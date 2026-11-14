#pragma once

#include "ui/semantic/semantic_review_io.hpp"

#include <QCoreApplication>

namespace sed::ui::semantic_review::detail
{

// Pure result presentation keeps service diagnostics independent of worker
// scheduling and file-system timing.
inline QString exportResultStatus(const ExportResult& result, const QString& destination)
{
    if ( result.success )
    {
        return QCoreApplication::translate("SemanticReviewDialog", "Created new report: %1")
            .arg(destination);
    }
    if ( !result.error.isEmpty() )
    {
        return result.error;
    }
    if ( result.code == ErrorCode::Cancelled )
    {
        return QCoreApplication::translate("SemanticReviewDialog", "Report export cancelled.");
    }
    return QCoreApplication::translate("SemanticReviewDialog", "Report export failed without a result.");
}

} // namespace sed::ui::semantic_review::detail
