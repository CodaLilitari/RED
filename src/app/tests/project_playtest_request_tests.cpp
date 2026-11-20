#include "ui/project/project_playtest_request.hpp"

#include <QJsonArray>
#include <iostream>
#include <stdexcept>

int main()
{
    using namespace sed::ui::project;
    const auto require = [](bool value, const char* message)
    {
        if ( !value ) throw std::runtime_error(message);
    };
    try
    {
        QJsonObject engine{{"role", "engine"}, {"destination", "openjkdf2"}};
        QJsonObject oldLevel{{"role", "scenario"}, {"destination", "episode/old.gob"}};
        QJsonObject machine{{"files", QJsonArray{engine, oldLevel}}, {"graphicsBackend", "xvfb"}};
        QJsonObject recipe{{"launch", QJsonObject{{"game", "jk"}, {"map", "bay.jkl"}, {"episode", "bay"}}}};
        const QString sha(64, 'a');
        const auto request = makeProjectPlaytestRequest(machine, recipe, "jk", "bay.jkl", "/tmp/owned.gob", sha);
        const auto files = request["files"].toArray();
        require(files.size() == 2 && files[0].toObject() == engine, "retains host engine, discards previous scenario");
        require(files[1].toObject()["sha256"] == sha && request["episodeArchive"] == "episode/bay.gob",
            "the selected prepared package is the sole staged episode");
        bool refused = false;
        try { makeProjectPlaytestRequest(machine, recipe, "mots", "bay.jkl", "/tmp/owned.goo", sha); }
        catch ( const std::exception& ) { refused = true; }
        require(refused, "reject a recipe targeting a different game");
        QJsonObject receipt{{"status", "completed"}, {"captureMode", "gameplay"}, {"processExitCode", 0},
            {"immutableInputsUnchanged", true}, {"gameplay", QJsonObject{{"status", "passed"}}},
            {"episodeArchive", QJsonObject{{"archiveSha256", sha}, {"looseEpisodeFilesStaged", false}}}};
        require(projectPlaytestStatus(receipt, sha, true) == "passed", "fresh passed scenario");
        require(projectPlaytestStatus(receipt, sha, false) == "historical", "changed workspace stales completed result");
        require(projectPlaytestStatus(receipt, QString(64, 'b'), true) == "unverified", "wrong package refuses success");
        receipt["processExitCode"] = 1;
        require(projectPlaytestStatus(receipt, sha, true) == "unverified", "failed exit refuses success");
        std::cout << "Project playtest bindings passed\n";
        return 0;
    }
    catch ( const std::exception& error )
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
