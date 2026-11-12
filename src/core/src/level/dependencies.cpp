#include "sed/level/dependencies.hpp"
#include "sed/assets/jkl.hpp"
#include "dependency_references.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <deque>
#include <map>
#include <stdexcept>
#include <utility>

namespace sed::level
{

std::span<const DependencyLimitSpec> dependencyLimitSpecs()
{
    static constexpr std::array specs{
        DependencyLimitSpec{"maxNodes", &DependencyLimits::maxNodes, 1, 16384},
        DependencyLimitSpec{"maxEdges", &DependencyLimits::maxEdges, 0, 65536},
        DependencyLimitSpec{"maxDepth", &DependencyLimits::maxDepth, 0, 1024},
        DependencyLimitSpec{"maxWorkUnits", &DependencyLimits::maxWorkUnits, 0, 4000000},
        DependencyLimitSpec{"maxIndexEntries", &DependencyLimits::maxIndexEntries, 0, 100000},
        DependencyLimitSpec{"maxPending", &DependencyLimits::maxPending, 0, 16384},
        DependencyLimitSpec{"maxScanBytes", &DependencyLimits::maxScanBytes, 0, 128u * 1024u * 1024u},
        DependencyLimitSpec{"maxResourceBytes", &DependencyLimits::maxResourceBytes, 0, 512u * 1024u * 1024u},
        DependencyLimitSpec{"maxStringBytes", &DependencyLimits::maxStringBytes, 0, 64u * 1024u * 1024u},
        DependencyLimitSpec{"maxDetailRecords", &DependencyLimits::maxDetailRecords, 0, 4096},
        DependencyLimitSpec{"maxDetailBytes", &DependencyLimits::maxDetailBytes, 0, 4u * 1024u * 1024u},
        DependencyLimitSpec{"maxIssues", &DependencyLimits::maxIssues, 0, 4096}
    };
    return specs;
}

namespace
{

std::string_view byteView(std::span<const std::byte> bytes)
{
    return {reinterpret_cast<const char*>(bytes.data()), bytes.size()};
}

std::string joined(std::string_view folder, std::string_view name)
{
    std::string value;
    value.reserve(folder.size() + (folder.empty() ? 0 : 1) + name.size());
    value.append(folder);
    if ( !folder.empty() )
    {
        value.push_back('/');
    }
    value.append(name);
    return value;
}

void lowercase(std::string& value, bool normalizeSlashes = false)
{
    for ( char& c : value )
    {
        if ( normalizeSlashes && c == '\\' )
        {
            c = '/';
        }
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
}

enum class Family { Text, Leaf, Ai, Unknown };

Family familyOf(std::string_view name)
{
    for ( const auto suffix : {".3do", ".pup", ".snd", ".spr", ".cog"} )
    {
        if ( detail::referenceEndsWithCi(name, suffix) )
        {
            return Family::Text;
        }
    }
    for ( const auto suffix : {".mat", ".wav", ".key", ".cmp"} )
    {
        if ( detail::referenceEndsWithCi(name, suffix) )
        {
            return Family::Leaf;
        }
    }
    return detail::referenceEndsWithCi(name, ".ai") ? Family::Ai : Family::Unknown;
}

void validate(const DependencyRequest& request)
{
    for ( const auto& spec : dependencyLimitSpecs() )
    {
        const auto value = request.limits.*spec.member;
        if ( value < spec.minimum || value > spec.maximum )
        {
            throw std::invalid_argument("dependency limit outside its bounds: " + std::string(spec.name));
        }
    }
    if ( request.inputBytes.size() > 128u * 1024u * 1024u )
    {
        throw std::invalid_argument("dependency source exceeds 128 MiB");
    }
    if ( request.levelFileName.empty() || request.levelFileName.size() > 255
        || request.levelFileName.find_first_of("/\\") != std::string_view::npos
        || request.levelFileName.find('\0') != std::string_view::npos
        || !detail::referenceEndsWithCi(request.levelFileName, ".jkl") )
    {
        throw std::invalid_argument("dependency source needs a plain JKL filename of at most 255 bytes");
    }
    if ( request.archives.size() > 16 || request.extraResources.size() > 1024 )
    {
        throw std::invalid_argument("dependency archive or extra reference count exceeds its bound");
    }
    std::size_t bytes{};
    std::size_t entries{};
    for ( const auto& input : request.archives )
    {
        const auto capture = input.archive.capturedBytes();
        if ( !capture || capture->size() > 512u * 1024u * 1024u - bytes
            || input.archive.entries().size() > 100000 - entries || input.label.size() > 4096 )
        {
            throw std::invalid_argument("dependency archives require bounded captured bytes and indexes");
        }
        bytes += capture->size();
        entries += input.archive.entries().size();
    }
    for ( const auto& extra : request.extraResources )
    {
        if ( extra.name.empty() || extra.name.size() > 1024 || extra.folder.size() > 128 )
        {
            throw std::invalid_argument("dependency extra reference exceeds its string bounds");
        }
    }
}

class Inspection
{
public:
    explicit Inspection(const DependencyRequest& request) : request_(request)
    {
        result_.limits = request.limits;
    }

    DependencyInspection run()
    {
        // Captured input hashes and bounded archive metadata are outside scan accounting.
        result_.inputSha256 = sed::assets::jklSourceSha256Hex(request_.inputBytes);
        for ( std::size_t i = 0; i < request_.archives.size(); ++i )
        {
            const auto& source = request_.archives[i];
            const auto capture = *source.archive.capturedBytes();
            result_.sourceArchives.push_back({i, source.label,
                sed::assets::jklSourceSha256Hex(byteView(capture)), capture.size(),
                source.archive.entries().size(), source.stock});
        }
        if ( strings(4 + request_.levelFileName.size() + result_.inputSha256.size()) )
        {
            DependencyNode level;
            level.path = joined("jkl", request_.levelFileName);
            level.sha256 = result_.inputSha256;
            nodes_.push_back(std::move(level));
            result_.counts.nodes = 1;
            buildIndexes();
        }
        if ( result_.scanComplete && scanBytes(request_.inputBytes.size()) )
        {
            const auto callback = callbacks(0);
            detail::visitRootReferences(request_.inputBytes, request_.extraResources, callback);
            if ( result_.scanComplete )
            {
                nodes_[0].inspection = DependencyScan::Scanned;
            }
        }
        while ( result_.scanComplete && !pending_.empty() )
        {
            active_ = pending_.front();
            pending_.pop_front();
            auto& node = nodes_[active_];
            const auto& provenance = *node.provenance;
            const auto capture = *request_.archives[provenance.archiveOrdinal].archive.capturedBytes();
            const auto bytes = byteView(capture.subspan(provenance.offset, provenance.bytes));
            if ( !scanBytes(bytes.size()) )
            {
                break;
            }
            const auto callback = callbacks(active_);
            // deque keeps the parent's strings stable while child admissions append nodes.
            detail::visitChildReferences(node.path, bytes, callback);
            if ( result_.scanComplete && node.inspection == DependencyScan::Pending )
            {
                node.inspection = DependencyScan::Scanned;
            }
        }
        return finish();
    }

private:
    const DependencyRequest& request_;
    DependencyInspection result_;
    std::deque<DependencyNode> nodes_;
    std::vector<DependencyEdge> edges_;
    std::vector<DependencyIssue> issues_;
    std::map<std::string, std::size_t, std::less<>> destinations_;
    std::vector<detail::ReferenceArchiveIndex> indexes_;
    std::deque<std::size_t> pending_;
    std::size_t active_{};

    static void unique(std::vector<std::string>& values, std::string_view value)
    {
        if ( std::find(values.begin(), values.end(), value) == values.end() )
        {
            values.emplace_back(value);
        }
    }

    void issue(std::size_t node, std::string_view code, std::string_view message)
    {
        ++result_.counts.issues;
        if ( issues_.size() >= request_.limits.maxIssues )
        {
            result_.detailsComplete = false;
            unique(result_.detailReasons, "maxIssues");
            return;
        }
        issues_.push_back({node, code, message});
    }

    bool stop(std::string_view reason)
    {
        if ( result_.scanComplete )
        {
            result_.scanComplete = false;
            unique(result_.scanReasons, reason);
            if ( active_ < nodes_.size() )
            {
                nodes_[active_].inspection = DependencyScan::BudgetStopped;
                issue(active_, "budgetStopped", "Inspection stopped at an operational limit.");
            }
        }
        return false;
    }

    bool charge(std::size_t& used, std::size_t maximum, std::size_t amount, std::string_view reason)
    {
        if ( !result_.scanComplete )
        {
            return false;
        }
        if ( amount > maximum - used )
        {
            return stop(reason);
        }
        used += amount;
        return true;
    }

    bool work()
    {
        return charge(result_.usage.workUnits, request_.limits.maxWorkUnits, 1, "maxWorkUnits");
    }

    bool strings(std::size_t amount)
    {
        return charge(result_.usage.stringBytes, request_.limits.maxStringBytes, amount, "maxStringBytes");
    }

    bool scanBytes(std::size_t amount)
    {
        return charge(result_.usage.scanBytes, request_.limits.maxScanBytes, amount, "maxScanBytes");
    }

    void buildIndexes()
    {
        indexes_.resize(request_.archives.size());
        for ( std::size_t a = 0; a < request_.archives.size(); ++a )
        {
            const auto& entries = request_.archives[a].archive.entries();
            for ( std::size_t row = 0; row < entries.size(); ++row )
            {
                const auto& name = entries[row].name;
                const auto base = detail::referenceBaseName(name);
                if ( !work() || !charge(result_.usage.indexEntries, request_.limits.maxIndexEntries,
                        1, "maxIndexEntries") || !strings(name.size() + base.size()) )
                {
                    return;
                }
                indexes_[a].add(detail::normalizeReferenceName(name),
                    detail::normalizeReferenceName(base), row);
            }
        }
    }

    bool cogAdmission(std::string_view bytes)
    {
        if ( bytes.size() > 256u * 1024u )
        {
            return stop("cogParserLimit");
        }
        std::size_t lines{};
        std::size_t evidenceBytes{};
        constexpr std::size_t kEvidenceLimit = 16u * 1024u * 1024u;
        for ( std::size_t start = 0; start < bytes.size(); )
        {
            if ( ++lines > 1024 )
            {
                return stop("cogParserLimit");
            }
            const auto newline = bytes.find('\n', start);
            const auto end = newline == std::string_view::npos ? bytes.size() : newline;
            const auto line = bytes.substr(start, end - start);
            std::size_t atoms{};
            bool inAtom = false;
            for ( const unsigned char character : line )
            {
                const bool separator = character == ',' || std::isspace(character) != 0;
                if ( !separator && !inAtom )
                {
                    ++atoms;
                }
                inAtom = !separator;
            }
            // The existing schema parser can copy this complete raw line into
            // one diagnostic per comma-separated modifier. Ignore quoting and
            // comments here to conservatively overcount, without allocating.
            // Eight additional copies cover declaration/block evidence; the
            // estimate includes string/row overhead, not a peak-memory promise.
            const auto copyCost = 2 * line.size() + 256;
            const auto copies = atoms + 8;
            if ( copies > (kEvidenceLimit - evidenceBytes) / copyCost )
            {
                return stop("cogParserLimit");
            }
            evidenceBytes += copies * copyCost;
            if ( newline == std::string_view::npos )
            {
                break;
            }
            start = newline + 1;
        }
        return true;
    }

    detail::ReferenceCallbacks callbacks(std::size_t parent)
    {
        detail::ReferenceCallbacks result;
        result.reference = [this, parent](detail::ReferenceView reference)
        {
            return referenceTo(parent, reference);
        };
        result.work = [this]() { return work(); };
        result.scope = [this](std::string_view scope) { unique(result_.unverifiedScopes, scope); };
        result.coverage = [this, parent](std::string_view code)
        {
            nodes_[parent].inspection = DependencyScan::Incomplete;
            issue(parent, code, "The supported reader could not inspect this resource completely.");
        };
        result.beforeCogParse = [this](std::string_view bytes) { return cogAdmission(bytes); };
        return result;
    }

    bool referenceTo(std::size_t parent, detail::ReferenceView reference);
    DependencyInspection finish();
};

bool Inspection::referenceTo(std::size_t parent, detail::ReferenceView reference)
{
    active_ = parent;
    if ( !work() )
    {
        return false;
    }
    if ( result_.counts.edges >= request_.limits.maxEdges )
    {
        return stop("maxEdges");
    }
    const auto base = detail::referenceBaseName(reference.name);
    const auto pathLength = reference.folder.size() + (reference.folder.empty() ? 0 : 1) + base.size();
    // Charge transient lookup keys as well as retained strings. This conservative
    // cumulative count prevents oversized authored tokens allocating before admission.
    if ( !strings(pathLength) )
    {
        return false;
    }
    std::string key = joined(reference.folder, base);
    // Package destination identity lowercases but does not rewrite extra-root folders.
    lowercase(key);
    if ( const auto existing = destinations_.find(key); existing != destinations_.end() )
    {
        if ( !strings(reference.name.size() + reference.folder.size()) )
        {
            return false;
        }
        edges_.push_back({parent, existing->second, std::string(reference.name),
            std::string(reference.folder), reference.relation, true});
        ++result_.counts.edges;
        return true;
    }
    if ( result_.counts.nodes >= request_.limits.maxNodes )
    {
        return stop("maxNodes");
    }
    const auto depth = nodes_[parent].depth + 1;
    if ( depth > request_.limits.maxDepth )
    {
        return stop("maxDepth");
    }

    std::optional<detail::ArchiveMatch> match;
    std::size_t archiveOrdinal{};
    bool ambiguous{};
    if ( !indexes_.empty() )
    {
        const auto folderLength = reference.folder.size() + (reference.folder.empty() ? 0 : 1)
            + reference.name.size();
        if ( !strings(folderLength + reference.name.size() + base.size()) )
        {
            return false;
        }
        std::string folderKey = joined(reference.folder, reference.name);
        lowercase(folderKey, true);
        const auto nameKey = detail::normalizeReferenceName(reference.name);
        const auto baseKey = detail::normalizeReferenceName(base);
        for ( std::size_t i = 0; i < indexes_.size(); ++i )
        {
            if ( !work() )
            {
                return false;
            }
            match = indexes_[i].exact(folderKey, nameKey);
            if ( match )
            {
                archiveOrdinal = i;
                break;
            }
        }
        if ( !match )
        {
            for ( std::size_t i = 0; i < indexes_.size(); ++i )
            {
                if ( !work() )
                {
                    return false;
                }
                bool localAmbiguous{};
                match = indexes_[i].basename(baseKey, localAmbiguous);
                ambiguous = ambiguous || localAmbiguous;
                if ( match )
                {
                    archiveOrdinal = i;
                    break;
                }
            }
        }
    }

    const auto family = familyOf(reference.name);
    if ( match && family == Family::Text && pending_.size() >= request_.limits.maxPending )
    {
        return stop("maxPending");
    }
    const sed::assets::GobEntry* entry = match
        ? &request_.archives[archiveOrdinal].archive.entries()[match->ordinal] : nullptr;
    if ( !strings(pathLength + reference.name.size() + reference.folder.size()
            + (entry ? 64 + entry->name.size() : 0)) )
    {
        return false;
    }
    if ( entry && !charge(result_.usage.resourceBytes, request_.limits.maxResourceBytes,
            entry->size, "maxResourceBytes") )
    {
        return false;
    }
    const auto id = nodes_.size();
    DependencyNode node;
    node.id = id;
    node.depth = depth;
    node.path = joined(reference.folder, base);
    if ( entry )
    {
        const auto& source = request_.archives[archiveOrdinal];
        const auto payload = source.archive.capturedBytes()->subspan(entry->offset, entry->size);
        node.resolution = DependencyResolution::Resolved;
        node.sha256 = sed::assets::jklSourceSha256Hex(byteView(payload));
        node.stock = source.stock;
        node.provenance = DependencyProvenance{archiveOrdinal, match->ordinal,
            entry->name, entry->offset, entry->size, match->tier};
        if ( family == Family::Leaf )
        {
            node.inspection = DependencyScan::Leaf;
        }
        else if ( family != Family::Text )
        {
            node.inspection = DependencyScan::Unsupported;
        }
    }
    else
    {
        node.resolution = ambiguous ? DependencyResolution::Ambiguous : DependencyResolution::Missing;
        node.inspection = DependencyScan::Incomplete;
    }
    nodes_.push_back(std::move(node));
    destinations_.emplace(std::move(key), id);
    edges_.push_back({parent, id, std::string(reference.name), std::string(reference.folder),
        reference.relation, false});
    ++result_.counts.nodes;
    ++result_.counts.edges;
    if ( !entry )
    {
        if ( ambiguous )
        {
            ++result_.counts.ambiguous;
            issue(id, "ambiguousResource", "More than one physical archive row matches the basename.");
        }
        else
        {
            ++result_.counts.missing;
            issue(id, "missingResource", "The reference was not found in the ordered resource archives.");
        }
    }
    else if ( family == Family::Text )
    {
        pending_.push_back(id);
        result_.usage.peakPending = std::max(result_.usage.peakPending, pending_.size());
    }
    else if ( family == Family::Ai || family == Family::Unknown )
    {
        ++result_.counts.unsupported;
        unique(result_.unverifiedScopes, family == Family::Ai
            ? "subresource-dependencies" : "unknown-resource-dependencies");
        issue(id, "unsupportedDependencies", "This resource family has no supported dependency reader.");
    }
    return true;
}

DependencyInspection Inspection::finish()
{
    if ( result_.counts.missing || result_.counts.ambiguous )
    {
        result_.allReferencesResolved = false;
    }
    else if ( !result_.scanComplete )
    {
        result_.allReferencesResolved.reset();
    }
    if ( !result_.scanComplete )
    {
        for ( auto& node : nodes_ )
        {
            if ( node.inspection == DependencyScan::Pending )
            {
                node.inspection = DependencyScan::BudgetStopped;
            }
        }
    }
    bool prefixOpen = true;
    const auto admit = [this, &prefixOpen](std::size_t cost)
    {
        if ( !prefixOpen )
        {
            return false;
        }
        std::string_view refusal;
        if ( result_.usage.detailRecords >= request_.limits.maxDetailRecords )
        {
            refusal = "maxDetailRecords";
        }
        else if ( cost > request_.limits.maxDetailBytes - result_.usage.detailBytes )
        {
            refusal = "maxDetailBytes";
        }
        if ( !refusal.empty() )
        {
            result_.detailsComplete = false;
            unique(result_.detailReasons, refusal);
            prefixOpen = false;
            return false;
        }
        ++result_.usage.detailRecords;
        result_.usage.detailBytes += cost;
        return true;
    };
    for ( auto& node : nodes_ )
    {
        const auto textBytes = node.path.size() + node.sha256.size()
            + (node.provenance ? node.provenance->entryName.size() + node.provenance->lookupTier.size() : 0);
        if ( admit(512 + 6 * textBytes) )
        {
            result_.nodes.push_back(std::move(node));
        }
    }
    for ( auto& edge : edges_ )
    {
        if ( admit(256 + 6 * (edge.authoredName.size() + edge.folder.size() + edge.relation.size())) )
        {
            result_.edges.push_back(std::move(edge));
        }
    }
    for ( const auto& issue : issues_ )
    {
        if ( admit(256 + 6 * (issue.code.size() + issue.message.size())) )
        {
            result_.issues.push_back(issue);
        }
    }
    return std::move(result_);
}

} // namespace

DependencyInspection inspectDependencies(const DependencyRequest& request)
{
    validate(request);
    return Inspection(request).run();
}

} // namespace sed::level
