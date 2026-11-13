#include "ui/diagnostics/installation_runtime_data.hpp"

#include <QCoreApplication>
#include <QFile>
#include <QJsonObject>
#include <QTemporaryDir>

#include <iostream>

int main(int argc, char** argv)
{
    QCoreApplication application(argc, argv);
    int failures{};
    const auto check = [&failures](const bool condition, const char* message)
    {
        if ( !condition )
        {
            ++failures;
            std::cerr << "FAIL: " << message << '\n';
        }
    };
    using namespace sed::ui::diagnostics;
    QTemporaryDir fixture;
    const auto write = [&fixture, &check](const QString& name, const QByteArray& bytes)
    {
        QFile file(fixture.filePath(name));
        check(file.open(QIODevice::WriteOnly | QIODevice::Truncate) && file.write(bytes) == bytes.size(),
            "synthetic runtime fixture was written");
    };
    for ( const auto& name : installationRuntimeDataNames() )
    {
        write(name, name.endsWith(".tpl") ? "fixture none type=ghost\n"
            : name.endsWith(".jlh") ? "LIST FIXTURE Synthetic\n"
            : name.endsWith(".jll") ? "[FIXTURE]\nfixture\n" : "source target\n");
    }
    QString error;
    const auto result = inspectInstallationRuntimeData(fixture.path(), &error);
    check(result && result->size() == 11, "all eleven runtime assets are parsed and reported");
    if ( result )
    {
        for ( const auto& item : *result )
        {
            check(item.toObject().value("sha256").toString().size() == 64
                && item.toObject().value("bytes").toInt() > 0, "runtime evidence records exact bounded source bytes");
        }
    }
    write("master.tpl", "# no templates\n");
    check(!inspectInstallationRuntimeData(fixture.path(), &error) && error.contains("master.tpl"),
        "non-parsing required template is refused with its name");
    write("master.tpl", "fixture none type=ghost\n");
    write("mats.jlh", "LIST MISSING Synthetic\n");
    check(!inspectInstallationRuntimeData(fixture.path(), &error), "unresolved category membership fails installed data check");
    write("mats.jlh", "LIST FIXTURE Synthetic\n");
    write("df2jk.lst", QByteArray(4 * 1024 * 1024 + 1, 'x'));
    check(!inspectInstallationRuntimeData(fixture.path(), &error), "oversized runtime data fails before parsing");
    write("df2jk.lst", "source target\n");
    check(QFile::remove(fixture.filePath("mots.tpl")), "required fixture removed deliberately");
    check(!inspectInstallationRuntimeData(fixture.path(), &error), "missing required runtime data fails atomically");
    if ( application.arguments().size() == 2 )
    {
        check(inspectInstallationRuntimeData(application.arguments()[1], &error).has_value(),
            "actual shipped project data passes its authoritative readers");
        if ( !error.isEmpty() )
        {
            std::cerr << error.toStdString() << '\n';
        }
    }
    std::cout << "installation runtime data failures=" << failures << '\n';
    return failures == 0 ? 0 : 1;
}
