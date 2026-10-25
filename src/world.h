#pragma once
#include "map.h"
#include <QHash>
#include <QJsonObject>
#include <QPointF>
#include <QVector>
#include <array>

namespace tw {
struct Input {
    bool up=false,down=false,left=false,right=false,fire=false,mine=false,wall=false;
    double aimX=0,aimY=0;
    static Input fromJson(const QJsonObject &j);
    QJsonObject json() const;
};
struct MatchOptions {
    int teams=2, captureSeconds=6, supplyMultiplier=1, respawnCost=10;
    bool friendlyFire=false;
    QJsonObject json() const;
    static MatchOptions fromJson(const QJsonObject &j);
};
struct Participant {int id=0,team=0;QString name,strategy="Collector",vehicle="Tank";bool bot=false,ready=false,loaded=false;QString status;int progress=0;QJsonObject json() const;static Participant fromJson(const QJsonObject &j);};

class World {
public:
    void start(const ArenaMap &map,const MatchOptions &settings,const QVector<Participant> &players);
    void setInput(int id,const Input &input);
    void enableBotTakeover(int id);
    QString upgrade(int id,const QString &kind,int padIndex=-1);
    void select(int id,const QString &type,int index=-1);
    QString requestRespawn(int id,const QString &vehicle);
    QString buildWall(int id,QPointF position);
    QJsonObject decorateForPlayer(int id,QJsonObject filtered) const;
    void step(double delta);
    QJsonObject snapshot() const;
    QJsonObject snapshotForTeam(int team,const QJsonObject &full) const;
    bool finished() const{return winner_>=0;}
    int winner() const{return winner_;}
private:
    struct Tank {int id=0, team=0;QString name,strategy;bool bot=false,alive=true;
        QPointF pos;double facing=0,health=100,respawn=0,fireCooldown=0;
        QString vehicle="Tank",selection="player",message,respawnVehicle;int selectionIndex=-1;
        int armor=0,gun=0,engine=0,radar=0,fireRate=0,shots=0,minesPlaced=0;
        bool mineHeld=false,wallHeld=false;double actionCooldown=0;
        double lastShotTime=-1000;double nextBotUpgrade=25,nextPathTime=0;QPointF waypoint;QVector<QString> upgradeHistory;Input input;
    };
    struct Base {QPointF pos;double hp=1200,maxHp=1200;int radar=0;};
    struct Node {QPointF pos;int resource=0,owner=-1,capturing=-1;double capture=0,incomeTime=0;};
    struct Pad {QPointF pos;int owner=-1,capturing=-1;double capture=0;};
    struct Turret {int id=0,team=0,padIndex=-1;QString type="Minigun";QPointF pos;double hp=280,maxHp=280,cooldown=0,aimAngle=0,lastShotTime=-1000;int rate=0,power=0,armor=0,radar=0,shots=0;};
    struct Bullet {QPointF pos,velocity;int owner=0,team=0;double damage=0,life=2;int id=0;QString kind="Cannon";};
    struct Explosion {QPointF pos;double life=0.45;int id=0;QString kind="Rocket";};
    struct Mine {QPointF pos;int team=0,owner=0;};
    struct Wall {QPoint tile;double hp=400;};
    struct Offer {std::array<int,ResourceCount> cost{};QString reason;};
    ArenaMap map_;MatchOptions options_;
    QVector<Tank> tanks_;QVector<Base> bases_;QVector<Node> nodes_;QVector<Pad> pads_;QVector<Turret> turrets_;QVector<Bullet> bullets_;
    QVector<Mine> mines_;QVector<Wall> walls_;QVector<Explosion> explosions_;
    QVector<std::array<int,ResourceCount>> supplies_;
    int winner_=-1,nextBulletId_=0;double elapsed_=0,baseIncome_=0;
    Tank *tank(int id);
    QPointF tileCenter(QPoint p) const;
    bool freeAt(QPointF p) const;
    bool hasFunds(int team,const std::array<int,ResourceCount> &cost) const;
    bool spend(int team,const std::array<int,ResourceCount> &cost);
    void fire(Tank &tank);
    double maxHealth(const Tank &tank) const;
    bool occupiedWall(QPoint tile)const;
    bool nearHome(const Tank &tank)const;
    Offer quote(const Tank &tank,const QString &kind,int index)const;
    void placeMine(Tank &tank);
    void placeWall(Tank &tank,QPointF position);
    void detonate(QPointF center,double strength,int team,const QString &kind);
    void botControl(Tank &tank);
    void damageTank(Tank &target,double amount,int killerTeam);
    void applyDeathPenalty(Tank &tank);
};
}
