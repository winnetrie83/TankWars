#include "world.h"
#include "visibility.h"
#include <QJsonArray>
#include <QQueue>
#include <QtMath>
#include <algorithm>
#include <cmath>

namespace tw {
static double distance(QPointF a,QPointF b){return std::hypot(a.x()-b.x(),a.y()-b.y());}
static QJsonObject xy(QPointF p){return {{"x",p.x()},{"y",p.y()}};}
static QPointF vec(const QJsonObject &j){return {j.value("x").toDouble(),j.value("y").toDouble()};}
static std::array<int,ResourceCount> price(int oil=0,int gold=0,int iron=0,int copper=0,int wood=0,int stone=0,int tech=0){return {oil,gold,iron,copper,wood,stone,tech};}
static QString insufficient(const std::array<int,ResourceCount> &have,const std::array<int,ResourceCount> &cost){
    QStringList missing;
    for(int r=0;r<ResourceCount;++r)if(have[r]<cost[r])missing<<QString("%1 %2").arg(cost[r]-have[r]).arg(resourceNames[r]);
    return missing.isEmpty()?QString():"Missing: "+missing.join(", ");
}
Input Input::fromJson(const QJsonObject &j){Input i;
    i.up=j.value("up").toBool();i.down=j.value("down").toBool();i.left=j.value("left").toBool();i.right=j.value("right").toBool();
    i.fire=j.value("fire").toBool();i.mine=j.value("mine").toBool();i.wall=j.value("wall").toBool();
    i.aimX=j.value("x").toDouble();i.aimY=j.value("y").toDouble();return i;
}
QJsonObject Input::json()const{return {{"up",up},{"down",down},{"left",left},{"right",right},{"fire",fire},{"mine",mine},{"wall",wall},{"x",aimX},{"y",aimY}};}
QJsonObject MatchOptions::json()const{return {{"teams",teams},{"capture",captureSeconds},{"supplies",supplyMultiplier},{"respawnCost",respawnCost},{"friendlyFire",friendlyFire}};}
MatchOptions MatchOptions::fromJson(const QJsonObject &j){MatchOptions o;
    o.teams=(j.value("teams").toInt(2)==4)?4:2;o.captureSeconds=std::clamp(j.value("capture").toInt(6),3,15);
    o.supplyMultiplier=std::clamp(j.value("supplies").toInt(1),1,3);
    o.respawnCost=std::clamp(j.value("respawnCost").toInt(10),5,30);
    o.friendlyFire=j.value("friendlyFire").toBool();return o;
}
QJsonObject Participant::json()const{return {{"id",id},{"name",name},{"team",team},{"bot",bot},{"strategy",strategy},{"vehicle",vehicle},{"ready",ready},{"loaded",loaded},{"status",status},{"progress",progress}};}
Participant Participant::fromJson(const QJsonObject &j){Participant p;
    p.id=j.value("id").toInt();p.name=j.value("name").toString();p.team=j.value("team").toInt();
    p.bot=j.value("bot").toBool();p.strategy=j.value("strategy").toString("Collector");p.vehicle=j.value("vehicle").toString("Tank");
    p.ready=j.value("ready").toBool();p.loaded=j.value("loaded").toBool();p.status=j.value("status").toString();p.progress=j.value("progress").toInt();return p;
}
World::Tank *World::tank(int id){for(auto &p:tanks_)if(p.id==id)return &p;return nullptr;}
QPointF World::tileCenter(QPoint p)const{return {(p.x()+0.5)*Tile,(p.y()+0.5)*Tile};}
bool World::occupiedWall(QPoint tile)const{for(const auto &w:walls_)if(w.hp>0&&w.tile==tile)return true;return false;}
bool World::freeAt(QPointF p)const{
    constexpr double r=10;
    for(QPointF d:{QPointF(-r,-r),QPointF(r,-r),QPointF(-r,r),QPointF(r,r)}){
        QPointF at=p+d;QPoint tile(int(std::floor(at.x()/Tile)),int(std::floor(at.y()/Tile)));
        if(!map_.passable(tile.x(),tile.y())||occupiedWall(tile))return false;
    }
    return true;
}
double World::maxHealth(const Tank &t)const{return (t.vehicle=="Tank"?200.0:100.0)+t.armor*32;}
bool World::nearHome(const Tank &t)const{return bases_[t.team].hp>0&&distance(t.pos,bases_[t.team].pos)<=9*Tile;}
bool World::hasFunds(int team,const std::array<int,ResourceCount> &cost)const{
    if(team<0||team>=supplies_.size())return false;
    for(int r=0;r<ResourceCount;++r)if(supplies_[team][r]<cost[r])return false;return true;
}
bool World::spend(int team,const std::array<int,ResourceCount> &cost){
    if(!hasFunds(team,cost))return false;
    for(int r=0;r<ResourceCount;++r)supplies_[team][r]-=cost[r];return true;
}
void World::start(const ArenaMap &map,const MatchOptions &settings,const QVector<Participant> &players){
    map_=map;options_=settings;winner_=-1;elapsed_=0;baseIncome_=0;nextBulletId_=0;
    tanks_.clear();bases_.clear();nodes_.clear();pads_.clear();turrets_.clear();bullets_.clear();mines_.clear();walls_.clear();supplies_.clear();
    for(int team=0;team<settings.teams;++team){
        Base b;b.pos=tileCenter(map.bases[team]);bases_.append(b);
        std::array<int,ResourceCount> bank{};bank.fill(25*settings.supplyMultiplier);bank[0]=35*settings.supplyMultiplier;supplies_.append(bank);
    }
    for(const auto &n:map.nodes)nodes_.append({tileCenter(n.tile),n.type});
    for(const auto &p:map.towerPads){Pad pad;pad.pos=tileCenter(p);
        // Map editor controls default ownership through the tower-pad position.
        for(int team=0;team<settings.teams;++team)if(distance(pad.pos,bases_[team].pos)<=6*Tile){pad.owner=team;break;}
        pads_.append(pad);
    }
    QVector<int> allocated(settings.teams,0);
    for(const auto &p:players){
        if(p.team<0||p.team>=settings.teams)continue;
        const int k=allocated[p.team]++;QPointF home=bases_[p.team].pos;
        QPointF where=home+QPointF((k%2?1:-1)*(70+12*(k/2)),(k%2?1:-1)*(55+12*(k/2)));
        if(!freeAt(where))where=home;
        Tank t;t.id=p.id;t.team=p.team;t.name=p.name;t.bot=p.bot;t.strategy=p.strategy;
        t.vehicle=(p.vehicle=="Jeep"||p.vehicle=="Missile launcher")?p.vehicle:"Tank";
        t.pos=where;t.health=maxHealth(t);t.facing=p.team%2?3.14:0;tanks_.append(t);
    }
}
void World::setInput(int id,const Input &input){if(auto *t=tank(id);t&&!t->bot)t->input=input;}
void World::enableBotTakeover(int id){if(auto *t=tank(id)){t->bot=true;t->strategy="Defender";t->name+=" (AI)";t->input={};}}
void World::select(int id,const QString &type,int index){
    auto *t=tank(id);if(!t)return;
    if(type=="player"){t->selection="player";t->selectionIndex=-1;}
    else if(type=="base"&&index>=0&&index<bases_.size()){t->selection=type;t->selectionIndex=index;}
    else if(type=="pad"&&index>=0&&index<pads_.size()){t->selection=type;t->selectionIndex=index;}
    else if(type=="node"&&index>=0&&index<nodes_.size()){t->selection=type;t->selectionIndex=index;}
}
World::Offer World::quote(const Tank &t,const QString &kind,int index)const{
    Offer offer;auto &cost=offer.cost;const int team=t.team;
    auto error=[&](const QString &r){offer.reason=r;};
    if(winner_>=0){error("Match ended");return offer;}
    if(!t.alive){error("Vehicle destroyed; wait for respawn");return offer;}
    bool personal=(kind=="armor"||kind=="gun"||kind=="engine"||kind=="radar"||kind=="fire_rate");
    if(personal){
        const int lv=kind=="armor"?t.armor:kind=="gun"?t.gun:kind=="engine"?t.engine:kind=="radar"?t.radar:t.fireRate;
        if(kind=="armor")cost=price(0,0,10+6*lv,0,0,7+3*lv);
        if(kind=="gun")cost=price(0,10+6*lv,0,7+4*lv,0,0,4+3*lv);
        if(kind=="engine")cost=price(9+5*lv,0,0,8+3*lv,6+2*lv);
        if(kind=="radar")cost=price(3+2*lv,0,0,9+4*lv,0,0,9+4*lv);
        if(kind=="fire_rate")cost=price(5+3*lv,7+4*lv,0,10+4*lv,0,0,8+3*lv);
        if(lv>=4){error("Maximum level (4) reached");return offer;}
        if(!nearHome(t))error("Return within 9 tiles of your living base");
    }else if(kind=="switch_jeep"||kind=="switch_tank"||kind=="switch_missile"){
        // Universal levels remain on the player, independent of the chassis.
        if(!nearHome(t))error("Change vehicles inside your base radius");
        const QString target=kind=="switch_jeep"?"Jeep":kind=="switch_tank"?"Tank":"Missile launcher";
        if(t.vehicle==target)error("Already driving this vehicle");
    }else if(kind=="base_hp"||kind=="base_radar"){
        if(kind=="base_hp")cost=price(0,0,26,0,15,28);
        else cost=price(8+3*bases_[team].radar,0,0,13+5*bases_[team].radar,0,0,15+6*bases_[team].radar);
        if(index!=team){error("Select your own base");return offer;}
        const auto &b=bases_[team];
        if(b.hp<=0){error("Base destroyed");return offer;}
        if(kind=="base_hp"){
            if(b.maxHp>=2400){error("Maximum base armor reached");return offer;}
            // Cost is also displayed when this action is unavailable.
        }else{
            if(b.radar>=4){error("Maximum base radar reached");return offer;}
            // Cost has already been calculated above.
        }
    }else if(kind.startsWith("tower_")){
        if(index<0||index>=pads_.size()){error("Select a tower platform");return offer;}
        if(kind=="tower_minigun")cost=price(4,0,16,16,7,0,3);
        if(kind=="tower_cannon")cost=price(3,6,24,15,0,8,7);
        if(kind=="tower_rocket")cost=price(8,10,16,18,0,0,19);
        const auto &pad=pads_[index];
        if(bases_[team].hp<=0){error("Your base is destroyed");return offer;}
        const Turret *built=nullptr;
        for(const auto &tr:turrets_)if(tr.padIndex==index&&tr.hp>0){built=&tr;break;}
        if(kind=="tower_minigun"||kind=="tower_cannon"||kind=="tower_rocket"){
            if(built){error("Platform occupied: destroy its tower before recapturing");return offer;}
        }else{
            if(!built){error("Build a tower first");return offer;}
            if(built->team!=team){error("Enemy tower; destroy it first");return offer;}
            int lv=kind=="tower_rate"?built->rate:kind=="tower_damage"?built->power:kind=="tower_armor"?built->armor:built->radar;
            if(kind=="tower_rate")cost=price(6+lv*3,4+lv*3,0,12+lv*5,0,0,8+lv*4);
            else if(kind=="tower_damage")cost=price(0,8+lv*4,10+lv*4,12+lv*4,0,0,10+lv*4);
            else if(kind=="tower_armor")cost=price(0,0,13+lv*7,0,0,17+lv*6);
            else if(kind=="tower_range")cost=price(8+lv*3,0,0,9+lv*3,0,0,12+lv*4);
            else error("Unknown tower upgrade");
            if(lv>=4){error("Maximum level (4) reached");return offer;}
        }
        if(pad.owner!=team){error("Capture this platform with your vehicle first");return offer;}
    }else {error("Unknown upgrade");return offer;}
    if(offer.reason.isEmpty())offer.reason=insufficient(supplies_[team],cost);
    return offer;
}
QString World::upgrade(int id,const QString &kind,int padIndex){
    Tank *t=tank(id);if(!t)return "Unknown player";
    const Offer offer=quote(*t,kind,padIndex);if(!offer.reason.isEmpty()){t->message=offer.reason;return offer.reason;}
    if(!spend(t->team,offer.cost)){t->message="Insufficient resources";return t->message;}
    if(kind=="armor"){++t->armor;t->health=std::min(maxHealth(*t),t->health+32);t->upgradeHistory.append(kind);}
    else if(kind=="gun"){++t->gun;t->upgradeHistory.append(kind);}
    else if(kind=="engine"){++t->engine;t->upgradeHistory.append(kind);}
    else if(kind=="radar"){++t->radar;t->upgradeHistory.append(kind);}
    else if(kind=="fire_rate"){++t->fireRate;t->upgradeHistory.append(kind);}
    else if(kind=="switch_jeep"||kind=="switch_tank"||kind=="switch_missile"){
        double healthRatio=t->health/std::max(1.0,maxHealth(*t));
        t->vehicle=kind=="switch_jeep"?"Jeep":kind=="switch_tank"?"Tank":"Missile launcher";
        t->health=std::max(1.0,maxHealth(*t)*healthRatio);
    }
    else if(kind=="base_hp"){bases_[t->team].maxHp+=200;bases_[t->team].hp=std::min(bases_[t->team].maxHp,bases_[t->team].hp+260);}
    else if(kind=="base_radar")++bases_[t->team].radar;
    else if(kind=="tower_minigun"||kind=="tower_cannon"||kind=="tower_rocket"){
        Turret tr;tr.padIndex=padIndex;tr.pos=pads_[padIndex].pos;tr.team=t->team;
        tr.type=kind=="tower_cannon"?"Cannon":kind=="tower_rocket"?"Rocket":"Minigun";turrets_.append(tr);
    }else{
        for(auto &tr:turrets_)if(tr.padIndex==padIndex&&tr.hp>0){
            if(kind=="tower_rate")++tr.rate;
            else if(kind=="tower_damage")++tr.power;
            else if(kind=="tower_armor"){++tr.armor;tr.maxHp+=85;tr.hp+=85;}
            else if(kind=="tower_range")++tr.radar;
            break;
        }
    }
    QString display=kind;display.replace('_',' ');t->message="Purchased "+display;return t->message;
}
void World::fire(Tank &t){
    if(!t.alive||t.fireCooldown>0)return;
    const double a=t.facing;QPointF dir(std::cos(a),std::sin(a));
    const double multiplier=t.vehicle=="Jeep"?0.5:t.vehicle=="Missile launcher"?2.0:1.0;
    bullets_.append({t.pos+dir*24,dir*(350.0+22*t.gun),t.id,t.team,(25.0+9*t.gun)*multiplier,2.0,++nextBulletId_});
    // Cooldown depends on both weapon power and dedicated rate-of-fire upgrade.
    t.fireCooldown=std::max(0.11,(0.64-0.045*t.gun)*std::pow(0.82,t.fireRate));
    ++t.shots;
}
void World::placeMine(Tank &t){
    if(t.vehicle!="Jeep"){t.message="Only the Jeep can lay mines";return;}
    if(t.minesPlaced>=8){t.message="Mine limit reached (8 active)";return;}
    auto cost=price(0,0,2,0,0,3);
    if(!spend(t.team,cost)){t.message="Mine requires 2 Iron + 3 Stone";return;}
    mines_.append({t.pos,t.team,t.id});++t.minesPlaced;t.message="Mine laid";
}
void World::placeWall(Tank &t){
    auto cost=price(0,0,0,0,0,12);
    const QPoint tile(int(std::floor(t.input.aimX/Tile)),int(std::floor(t.input.aimY/Tile)));
    if(!map_.passable(tile.x(),tile.y())||occupiedWall(tile)){t.message="Place walls only on empty grass";return;}
    QPointF center=tileCenter(tile);
    if(distance(t.pos,center)>5*Tile){t.message="Wall placement: maximum range is 5 tiles";return;}
    for(const auto &b:bases_)if(distance(b.pos,center)<3*Tile){t.message="Cannot block base entrances";return;}
    for(const auto &n:nodes_)if(distance(n.pos,center)<2*Tile){t.message="Cannot block resource platforms";return;}
    for(const auto &pad:pads_)if(distance(pad.pos,center)<2*Tile){t.message="Cannot block tower platforms";return;}
    for(const auto &other:tanks_)if(other.alive&&distance(other.pos,center)<34){t.message="Vehicle blocks wall placement";return;}
    // Do not seal pathways to critical objectives.
    QVector<bool> blocked(map_.width*map_.height,false);
    for(const auto &w:walls_)if(w.hp>0)blocked[w.tile.y()*map_.width+w.tile.x()]=true;
    blocked[tile.y()*map_.width+tile.x()]=true;
    for(int team=0;team<options_.teams;++team){
        QPoint origin=map_.bases[team];int first=origin.y()*map_.width+origin.x();
        if(blocked[first]){t.message="Wall would block base";return;}
        QVector<bool> visited(map_.width*map_.height,false);QQueue<int> q;visited[first]=true;q.enqueue(first);
        while(!q.isEmpty()){
            int at=q.dequeue(),x=at%map_.width,y=at/map_.width;
            for(QPoint d:{QPoint(1,0),QPoint(-1,0),QPoint(0,1),QPoint(0,-1)}){
                int nx=x+d.x(),ny=y+d.y();if(!map_.passable(nx,ny))continue;
                int j=ny*map_.width+nx;if(visited[j]||blocked[j])continue;visited[j]=true;q.enqueue(j);
            }
        }
        bool possible=true;
        for(const auto &n:map_.nodes)if(!visited[n.tile.y()*map_.width+n.tile.x()]){possible=false;break;}
        if(possible)for(auto pad:map_.towerPads)if(!visited[pad.y()*map_.width+pad.x()]){possible=false;break;}
        if(!possible){t.message="Wall would isolate an objective";return;}
    }
    if(!spend(t.team,cost)){t.message="Wall requires 12 Stone";return;}
    walls_.append({tile,400});t.message="Stone wall built";
}
void World::botControl(Tank &t){
    if(!t.alive)return;
    QPointF target=bases_[(t.team+1)%options_.teams].pos;
    if(t.strategy=="Collector"){
        double best=1e9;
        for(const auto &n:nodes_)if(n.owner!=t.team){double score=distance(n.pos,t.pos);if(score<best){best=score;target=n.pos;}}
        for(const auto &pad:pads_)if(pad.owner!=t.team){double score=distance(pad.pos,t.pos)+85;if(score<best){best=score;target=pad.pos;}}
    }else if(t.strategy=="Defender"){
        target=bases_[t.team].pos+QPointF(55,0);double near=300;
        for(const auto &enemy:tanks_)if(enemy.alive&&enemy.team!=t.team&&distance(enemy.pos,bases_[t.team].pos)<near){target=enemy.pos;near=distance(enemy.pos,bases_[t.team].pos);}
    }else{
        double best=1e9;for(const auto &enemy:tanks_)if(enemy.alive&&enemy.team!=t.team&&distance(enemy.pos,t.pos)<best){best=distance(enemy.pos,t.pos);target=enemy.pos;}
    }
    QPointF fireTarget=target;double enemyDist=1e9;
    for(const auto &enemy:tanks_)if(enemy.alive&&enemy.team!=t.team&&distance(enemy.pos,t.pos)<enemyDist){enemyDist=distance(enemy.pos,t.pos);fireTarget=enemy.pos;}
    if(enemyDist>300)for(int team=0;team<bases_.size();++team)if(team!=t.team&&bases_[team].hp>0&&distance(bases_[team].pos,t.pos)<enemyDist){enemyDist=distance(bases_[team].pos,t.pos);fireTarget=bases_[team].pos;}
    t.input={};QPointF waypoint=t.waypoint;
    if(elapsed_>=t.nextPathTime||waypoint.isNull()||(distance(waypoint,t.pos)<19&&distance(target,t.pos)>35)){
        t.nextPathTime=elapsed_+0.75+(t.id%5)*0.05;waypoint=target;
        QPoint start(int(t.pos.x()/Tile),int(t.pos.y()/Tile)),goal(int(target.x()/Tile),int(target.y()/Tile));
        if(map_.validPoint(start)&&map_.validPoint(goal)&&map_.passable(goal.x(),goal.y())){
            QVector<int> parent(map_.width*map_.height,-1);QQueue<int> open;
            int first=start.y()*map_.width+start.x(),last=goal.y()*map_.width+goal.x();parent[first]=first;open.enqueue(first);
            while(!open.isEmpty()&&parent[last]<0){
                int at=open.dequeue(),x=at%map_.width,y=at/map_.width;
                for(QPoint d:{QPoint(1,0),QPoint(0,1),QPoint(-1,0),QPoint(0,-1)}){
                    int nx=x+d.x(),ny=y+d.y();if(!map_.passable(nx,ny)||occupiedWall({nx,ny}))continue;
                    int next=ny*map_.width+nx;if(parent[next]>=0)continue;parent[next]=at;open.enqueue(next);
                }
            }
            if(parent[last]>=0&&first!=last){int at=last;while(parent[at]!=first&&parent[at]!=at)at=parent[at];waypoint=tileCenter({at%map_.width,at/map_.width});}
        }
        t.waypoint=waypoint;
    }
    QPointF dir=waypoint-t.pos;if(distance(target,t.pos)>35){t.input.left=dir.x()<-5;t.input.right=dir.x()>5;t.input.up=dir.y()<-5;t.input.down=dir.y()>5;}
    t.input.aimX=fireTarget.x();t.input.aimY=fireTarget.y();t.input.fire=enemyDist<340;
    if(t.strategy=="Collector"&&distance(target,t.pos)<65)t.input.fire=enemyDist<260;
    if(elapsed_>=t.nextBotUpgrade){t.nextBotUpgrade=elapsed_+22;
        if(nearHome(t))upgrade(t.id,t.strategy=="Defender"?"armor":(t.gun>=4?"fire_rate":"gun"));
        else if(t.vehicle=="Jeep"&&enemyDist<125&&t.minesPlaced<8)placeMine(t);
    }
}
void World::applyDeathPenalty(Tank &t){
    if(t.upgradeHistory.isEmpty())return;
    const QString last=t.upgradeHistory.takeLast();
    if(last=="armor"&&t.armor>0)--t.armor;
    else if(last=="gun"&&t.gun>0)--t.gun;
    else if(last=="engine"&&t.engine>0)--t.engine;
    else if(last=="radar"&&t.radar>0)--t.radar;
    else if(last=="fire_rate"&&t.fireRate>0)--t.fireRate;
}
void World::damageTank(Tank &target,double amount,int killerTeam){
    if(!target.alive)return;
    target.health-=amount;if(target.health>0)return;
    target.health=0;target.alive=false;target.respawn=10;applyDeathPenalty(target);
    if(killerTeam>=0&&killerTeam<supplies_.size()&&killerTeam!=target.team)
        for(int r=0;r<ResourceCount;++r){int amount=std::min(4,supplies_[target.team][r]);supplies_[target.team][r]-=amount;supplies_[killerTeam][r]+=amount;}
}
void World::step(double dt){
    if(winner_>=0)return;dt=std::clamp(dt,0.0,0.10);elapsed_+=dt;
    baseIncome_+=dt;
    while(baseIncome_>=5.0){baseIncome_-=5.0;for(int team=0;team<bases_.size();++team)if(bases_[team].hp>0){++supplies_[team][0];++supplies_[team][2];}}
    for(auto &t:tanks_){
        if(!t.alive){
            if(bases_[t.team].hp>0){t.respawn=std::max(0.0,t.respawn-dt);
                if(t.respawn<=0){auto cost=price(options_.respawnCost,0,options_.respawnCost);
                    if(spend(t.team,cost)){t.alive=true;t.health=maxHealth(t);
                        QPointF p=bases_[t.team].pos+QPointF(65,60);t.pos=freeAt(p)?p:bases_[t.team].pos;t.input={};}
                }
            }
            continue;
        }
        t.fireCooldown=std::max(0.0,t.fireCooldown-dt);t.actionCooldown=std::max(0.0,t.actionCooldown-dt);
        if(t.bot)botControl(t);
        const auto i=t.input;
        double dx=double(i.right)-double(i.left),dy=double(i.down)-double(i.up);double len=std::hypot(dx,dy);
        double speed=t.vehicle=="Jeep"?161:t.vehicle=="Missile launcher"?132:120;speed+=17*t.engine;
        if(len>0){dx/=len;dy/=len;
            QPointF px=t.pos+QPointF(dx*speed*dt,0),py=t.pos+QPointF(0,dy*speed*dt);
            if(freeAt(px))t.pos.setX(px.x());if(freeAt(py))t.pos.setY(py.y());
        }
        QPointF aim(i.aimX-t.pos.x(),i.aimY-t.pos.y());if(std::hypot(aim.x(),aim.y())>2)t.facing=std::atan2(aim.y(),aim.x());
        if(i.fire)fire(t);
        if(i.mine&&!t.mineHeld&&t.actionCooldown<=0){placeMine(t);t.actionCooldown=0.3;}
        if(i.wall&&!t.wallHeld&&t.actionCooldown<=0){placeWall(t);t.actionCooldown=0.3;}
        t.mineHeld=i.mine;t.wallHeld=i.wall;
    }
    // Friendly mines are invisible to opponents even in full vision until triggered.
    for(int m=mines_.size()-1;m>=0;--m){auto mine=mines_[m];bool exploded=false;
        for(auto &t:tanks_)if(t.alive&&t.team!=mine.team&&distance(t.pos,mine.pos)<19){damageTank(t,125,mine.team);exploded=true;break;}
        if(exploded){mines_.removeAt(m);if(auto *owner=tank(mine.owner))owner->minesPlaced=std::max(0,owner->minesPlaced-1);}
    }
    for(auto &node:nodes_){
        std::array<double,4> presence{};presence.fill(0);
        for(const auto &t:tanks_)if(t.alive&&distance(t.pos,node.pos)<66)presence[t.team]+=(t.vehicle=="Jeep"?2.0:1.0);
        int contest=-1,teams=0;double captureBoost=1;
        for(int team=0;team<options_.teams;++team)if(presence[team]>0){contest=team;++teams;captureBoost=std::min(2.0,presence[team]);}
        if(teams==1&&contest!=node.owner){
            if(node.capturing!=contest){node.capturing=contest;node.capture=0;}
            node.capture=std::min(100.0,node.capture+100.0/(options_.captureSeconds*2.0)*dt*captureBoost);
            if(node.capture>=100){node.owner=contest;node.capturing=-1;node.capture=0;node.incomeTime=0;}
        }else if(teams==0&&node.capture>0){node.capture=std::max(0.0,node.capture-9*dt);if(node.capture<=0)node.capturing=-1;}
        if(node.owner>=0){node.incomeTime+=dt;while(node.incomeTime>=2){supplies_[node.owner][node.resource]+=2;node.incomeTime-=2;}}
    }
    for(int index=0;index<pads_.size();++index){
        auto &pad=pads_[index];bool occupied=false;
        for(const auto &tower:turrets_)if(tower.hp>0&&tower.padIndex==index){occupied=true;break;}
        if(occupied){pad.capturing=-1;pad.capture=0;continue;}
        std::array<double,4> presence{};presence.fill(0);
        for(const auto &t:tanks_)if(t.alive&&distance(t.pos,pad.pos)<65)presence[t.team]+=(t.vehicle=="Jeep"?2.0:1.0);
        int contest=-1,teams=0;double boost=1;
        for(int team=0;team<options_.teams;++team)if(presence[team]>0){contest=team;++teams;boost=std::min(2.0,presence[team]);}
        if(teams==1&&contest!=pad.owner){
            if(pad.capturing!=contest){pad.capturing=contest;pad.capture=0;}
            pad.capture=std::min(100.0,pad.capture+(100.0/options_.captureSeconds)*dt*boost);
            if(pad.capture>=100){pad.owner=contest;pad.capturing=-1;pad.capture=0;}
        }else if(teams==0&&pad.capture>0){pad.capture=std::max(0.0,pad.capture-15*dt);if(pad.capture<=0)pad.capturing=-1;}
    }
    for(auto &tower:turrets_){
        if(tower.hp<=0||bases_[tower.team].hp<=0)continue;tower.cooldown=std::max(0.0,tower.cooldown-dt);
        const double radius=(tower.type=="Rocket"?320:tower.type=="Cannon"?280:250)+tower.radar*46;
        Tank *target=nullptr;double best=radius;
        for(auto &enemy:tanks_)if(enemy.alive&&enemy.team!=tower.team&&distance(enemy.pos,tower.pos)<best){best=distance(enemy.pos,tower.pos);target=&enemy;}
        if(target&&tower.cooldown<=0){QPointF dir=target->pos-tower.pos;double d=distance(target->pos,tower.pos);
            if(d>1){double vel=tower.type=="Rocket"?265:440;double dmg=tower.type=="Rocket"?65:tower.type=="Cannon"?42:14;
                bullets_.append({tower.pos+dir*(28/d),dir*(vel/d),-1,tower.team,dmg+tower.power*8,2,++nextBulletId_});
                double interval=tower.type=="Rocket"?1.65:tower.type=="Cannon"?1.05:0.22;
                tower.cooldown=std::max(0.10,interval*std::pow(0.8,tower.rate));++tower.shots;
            }
        }
    }
    for(auto &bullet:bullets_){
        if(bullet.life<=0)continue;bullet.life-=dt;
        const int n=std::max(1,int(std::ceil(std::hypot(bullet.velocity.x(),bullet.velocity.y())*dt/9)));
        for(int s=0;s<n&&bullet.life>0;++s){
            bullet.pos+=bullet.velocity*(dt/n);
            QPoint tile(int(bullet.pos.x()/Tile),int(bullet.pos.y()/Tile));
            if(!map_.passable(tile.x(),tile.y())){bullet.life=0;break;}
            bool hit=false;
            for(auto &wall:walls_)if(wall.hp>0&&wall.tile==tile){wall.hp=std::max(0.0,wall.hp-bullet.damage);hit=true;break;}
            if(!hit)for(auto &t:tanks_)if(t.alive&&t.id!=bullet.owner&&(options_.friendlyFire||t.team!=bullet.team)&&distance(t.pos,bullet.pos)<15){damageTank(t,bullet.damage,bullet.team);hit=true;break;}
            if(!hit)for(int team=0;team<bases_.size();++team)if(bases_[team].hp>0&&(options_.friendlyFire||team!=bullet.team)&&distance(bases_[team].pos,bullet.pos)<46){bases_[team].hp=std::max(0.0,bases_[team].hp-bullet.damage);hit=true;break;}
            if(!hit)for(auto &tr:turrets_)if(tr.hp>0&&(options_.friendlyFire||tr.team!=bullet.team)&&distance(tr.pos,bullet.pos)<29){tr.hp=std::max(0.0,tr.hp-bullet.damage);hit=true;break;}
            if(hit)bullet.life=0;
        }
    }
    bullets_.erase(std::remove_if(bullets_.begin(),bullets_.end(),[](const Bullet &b){return b.life<=0;}),bullets_.end());
    turrets_.erase(std::remove_if(turrets_.begin(),turrets_.end(),[](const Turret &t){return t.hp<=0;}),turrets_.end());
    walls_.erase(std::remove_if(walls_.begin(),walls_.end(),[](const Wall &w){return w.hp<=0;}),walls_.end());
    int alive=0,last=-1;
    for(int team=0;team<options_.teams;++team){bool present=bases_[team].hp>0;
        for(const auto &t:tanks_)if(t.team==team&&t.alive)present=true;
        if(present){++alive;last=team;}
    }
    if(alive<=1)winner_=last;
}
QJsonObject World::snapshot()const{
    QJsonArray players,bases,nodes,pads,towers,bullets,banks,mines,walls;
    for(const auto &t:tanks_){auto j=xy(t.pos);j["id"]=t.id;j["name"]=t.name;j["team"]=t.team;j["bot"]=t.bot;
        j["vehicle"]=t.vehicle;j["alive"]=t.alive;j["hp"]=t.health;j["maxHp"]=maxHealth(t);j["angle"]=t.facing;
        j["respawn"]=t.respawn;j["armor"]=t.armor;j["gun"]=t.gun;j["engine"]=t.engine;j["radar"]=t.radar;
        j["fire_rate"]=t.fireRate;j["shots"]=t.shots;players.append(j);
    }
    for(int team=0;team<bases_.size();++team){auto j=xy(bases_[team].pos);j["team"]=team;j["hp"]=bases_[team].hp;j["maxHp"]=bases_[team].maxHp;j["radar"]=bases_[team].radar;bases.append(j);}
    for(int index=0;index<nodes_.size();++index){const auto &n=nodes_[index];auto j=xy(n.pos);j["id"]=index;j["resource"]=n.resource;j["owner"]=n.owner;j["capturing"]=n.capturing;j["progress"]=n.capture;nodes.append(j);}
    for(int index=0;index<pads_.size();++index){const auto &pad=pads_[index];auto j=xy(pad.pos);j["id"]=index;j["owner"]=pad.owner;j["capturing"]=pad.capturing;j["progress"]=pad.capture;pads.append(j);}
    for(const auto &tr:turrets_){auto j=xy(tr.pos);j["team"]=tr.team;j["pad"]=tr.padIndex;j["hp"]=tr.hp;j["maxHp"]=tr.maxHp;j["type"]=tr.type;
        j["rate"]=tr.rate;j["power"]=tr.power;j["armor"]=tr.armor;j["range"]=tr.radar;j["shots"]=tr.shots;towers.append(j);}
    for(const auto &b:bullets_){auto j=xy(b.pos);j["team"]=b.team;j["id"]=b.id;j["owner"]=b.owner;bullets.append(j);}
    for(const auto &m:mines_){auto j=xy(m.pos);j["team"]=m.team;mines.append(j);}
    for(const auto &w:walls_){auto j=xy(tileCenter(w.tile));j["hp"]=w.hp;walls.append(j);}
    for(const auto &bank:supplies_){QJsonArray a;for(int quantity:bank)a.append(quantity);banks.append(a);}
    return {{"type","snapshot"},{"time",elapsed_},{"winner",winner_},{"players",players},{"bases",bases},{"nodes",nodes},
            {"pads",pads},{"towers",towers},{"bullets",bullets},{"banks",banks},{"mines",mines},{"walls",walls}};
}
QJsonObject World::snapshotForTeam(int team,const QJsonObject &full)const{
    if(team<0||team>=options_.teams)return full;
    QVector<VisionSource> sources;
    if(bases_[team].hp>0)sources.append({bases_[team].pos,8+3*bases_[team].radar});
    for(const auto &t:tanks_)if(t.alive&&t.team==team)sources.append({t.pos,9+3*t.radar});
    for(const auto &tr:turrets_)if(tr.hp>0&&tr.team==team)sources.append({tr.pos,7+2*tr.radar});
    ArenaMap sight=map_;
    for(const auto &wall:walls_)if(wall.hp>0&&map_.validPoint(wall.tile))sight.terrain[wall.tile.y()*map_.width+wall.tile.x()]=1;
    const QByteArray vision=visibleTiles(sight,sources);
    auto visible=[&](const QJsonObject &j){return isVisibleAt(sight,vision,vec(j));};
    QJsonObject filtered=full;
    for(const QString &key:{QString("players"),QString("bases"),QString("towers"),QString("bullets"),QString("nodes"),QString("pads"),QString("walls"),QString("mines")}){
        QJsonArray result;
        for(const auto &entry:full.value(key).toArray()){
            const QJsonObject j=entry.toObject();
            if(key=="mines"){if(j.value("team").toInt(-1)==team)result.append(j);continue;}
            if((key=="players"||key=="bases"||key=="towers")&&j.value("team").toInt(-1)==team){result.append(j);continue;}
            if(key=="pads"&&j.value("owner").toInt(-1)==team){result.append(j);continue;}
            if(visible(j))result.append(j);
        }
        filtered[key]=result;
    }
    QJsonArray banks;const auto raw=full.value("banks").toArray();
    for(int k=0;k<raw.size();++k)banks.append(k==team?raw.at(k):QJsonValue(QJsonArray{}));
    filtered["banks"]=banks;
    return filtered;
}
QJsonObject World::decorateForPlayer(int id,QJsonObject filtered)const{
    const Tank *t=nullptr;for(const auto &p:tanks_)if(p.id==id){t=&p;break;}
    if(!t)return filtered;
    QString type=t->selection;int index=t->selectionIndex;
    QJsonArray offers;
    const auto add=[&](const QString &kind,const QString &label){const Offer o=quote(*t,kind,index);
        QJsonArray cost;for(int n:o.cost)cost.append(n);
        offers.append(QJsonObject{{"kind",kind},{"label",label},{"cost",cost},{"allowed",o.reason.isEmpty()},{"reason",o.reason}});
    };
    if(type=="player"){
        add("armor","Armor");add("gun","Weapon damage");add("engine","Movement speed");
        add("radar","Player radar");add("fire_rate","Weapon fire rate");
        add("switch_jeep","Change vehicle: Jeep");add("switch_tank","Change vehicle: Tank");add("switch_missile","Change vehicle: Missile launcher");
    }else if(type=="base"){
        add("base_hp","Base armor / repair");add("base_radar","Base radar");
    }else if(type=="pad"){
        bool built=false;
        for(const auto &tr:turrets_)if(tr.padIndex==index&&tr.hp>0){built=true;break;}
        if(built){add("tower_rate","Tower fire rate");add("tower_damage","Tower damage");add("tower_armor","Tower armor");add("tower_range","Tower radar / range");}
        else {add("tower_minigun","Build Minigun tower");add("tower_cannon","Build Cannon tower");add("tower_rocket","Build Rocket tower");}
    }
    filtered["offers"]=offers;filtered["selection"]=type;filtered["selectionIndex"]=index;filtered["feedback"]=t->message;
    return filtered;
}
}
