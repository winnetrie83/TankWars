#pragma once
#include <QJsonObject>
#include <QPoint>
#include <QString>
#include <QStringList>
#include <QVector>

namespace tw {
constexpr int Tile = 32;
inline const QStringList resourceNames = {"Oil", "Gold", "Iron", "Copper", "Wood", "Stone", "Tech"};
// Resource slots: oil, gold, iron, copper, wood, stone, tech (7).
constexpr int ResourceCount = 7;
inline constexpr int Version = 1;  // JSON map format; original maps remain compatible.
inline constexpr int ProtocolVersion = 4;  // Per-team fog-of-war network protocol.

struct ResourceNode { QPoint tile; int type=0; };
struct ArenaMap {
    QString name = "Crossroads";
    int width = 32, height = 22;
    QVector<int> terrain; // 0 grass, 1 wall, 2 water
    QVector<QPoint> bases; // four potential team spawn positions
    QVector<ResourceNode> nodes;
    QVector<QPoint> towerPads;
    bool passable(int x,int y) const;
    bool validPoint(QPoint p) const;
    QJsonObject toJson() const;
    static bool fromJson(const QJsonObject &json, ArenaMap &out, QString *error=nullptr);
    bool validate(int teams, QString *error=nullptr) const;
    static ArenaMap standard();
};
QString mapDirectory();
QString cleanMapFilename(const QString &name);
bool saveMap(const ArenaMap &map, const QString &path, QString *error=nullptr);
bool loadMap(const QString &path, ArenaMap &map, QString *error=nullptr);
}
