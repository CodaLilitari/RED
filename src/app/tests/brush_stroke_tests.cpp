#include "ui/viewport/brush_stroke.hpp"
#include <iostream>

int main()
{
    using namespace sed::ui::viewport;
    sed::Level level{};
    sed::Sector room{};
    room.id = 17;
    level.sectors.push_back(room);
    sed::Thing thing{};
    thing.name = thing.templateName = "crate";
    thing.sector = 0;
    thing.x = 0.123456789123;
    thing.yaw = 37.251;
    level.things.push_back(thing);
    const auto data = captureBrushStroke(level, 0);
    if ( data.isEmpty() ) return 1;
    const auto contains = [](const auto&, const auto&) { return true; };
    std::vector<sed::Thing> planned;
    if ( planBrushStroke(level, data, "crate", contains, planned).isEmpty() || !planned.empty() ) return 2;
    level.things.clear();
    if ( !planBrushStroke(level, data, "crate", contains, planned).isEmpty()
        || planned.size() != 1 || planned[0].x != thing.x || planned[0].yaw != thing.yaw ) return 3;
    if ( planBrushStroke(level, data, "wrong", contains, planned).isEmpty() || !planned.empty() ) return 4;
    level.sectors[0].id = 99;
    if ( planBrushStroke(level, data, "crate", contains, planned).isEmpty() || !planned.empty() ) return 5;
    if ( planBrushStroke(level, "{}", "crate", contains, planned).isEmpty() ) return 6;
    level.things.resize(kBrushStrokePoseLimit + 1, thing);
    if ( !captureBrushStroke(level, 0).isEmpty() ) return 7;
    level.things.clear();
    level.sectors[0].id = 17;
    const auto outside = [](const auto&, const auto&) { return false; };
    if ( planBrushStroke(level, data, "crate", outside, planned).isEmpty() || !planned.empty() ) return 8;
    auto object = QJsonDocument::fromJson(data).object();
    auto rows = object.value("poses").toArray();
    rows.append(QJsonArray{0, 17, "not a coordinate"});
    object.insert("poses", rows);
    if ( planBrushStroke(level, QJsonDocument(object).toJson(), "crate", contains, planned).isEmpty()
        || !planned.empty() ) return 9;
    std::cout << "brush stroke contracts passed\n";
}
