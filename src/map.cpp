#include "map.h"
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QQueue>
#include <QSet>
#include <QRegularExpression>
#include <QStandardPaths>
#include <algorithm>
#include <cmath>

namespace tw {
static QJsonObject pointJson(QPoint p) { return {{"x",p.x()},{"y",p.y()}}; }
static QPoint jsonPoint(const QJsonValue &v) { auto o=v.toObject(); return {o.value("x").toInt(-1),o.value("y").toInt(-1)}; }
bool ArenaMap::validPoint(QPoint p) const {return p.x()>=0&&p.y()>=0&&p.x()<width&&p.y()<height;}
bool ArenaMap::passable(int x,int y) const {return x>=0&&y>=0&&x<width&&y<height&&terrain.value(y*width+x,1)==0;}
QJsonObject ArenaMap::toJson() const {
    QJsonArray t,b,n,p;
    for(int v:terrain)t.append(v);
    for(auto v:bases)b.append(pointJson(v));
    for(const auto &v:nodes){auto j=pointJson(v.tile);j["resource"]=v.type;n.append(j);}
    for(auto v:towerPads)p.append(pointJson(v));
    return {{"version",Version},{"name",name},{"width",width},{"height",height},
            {"terrain",t},{"bases",b},{"nodes",n},{"towerPads",p}};
}
bool ArenaMap::fromJson(const QJsonObject &j,ArenaMap &out,QString *error) {
    auto fail=[&](const QString &s){if(error)*error=s;return false;};
    if(j.value("version").toInt()!=Version)return fail("Unsupported map version.");
    ArenaMap m; m.width=j.value("width").toInt();m.height=j.value("height").toInt();
    if(m.width<20||m.width>192||m.height<16||m.height>128)return fail("Map size out of bounds (20..192 x 16..128).");
    m.name=j.value("name").toString().trimmed().left(60);
    if(m.name.isEmpty())return fail("Map name is required.");
    auto tiles=j.value("terrain").toArray();
    if(tiles.size()!=m.width*m.height)return fail("Incorrect terrain array size.");
    for(const auto &v:tiles){int a=v.toInt(-1);if(a<0||a>2)return fail("Invalid terrain tile.");m.terrain.append(a);}
    for(const auto &v:j.value("bases").toArray())m.bases.append(jsonPoint(v));
    for(const auto &v:j.value("nodes").toArray()){
        auto o=v.toObject();m.nodes.append({jsonPoint(v),o.value("resource").toInt(-1)});
    }
    for(const auto &v:j.value("towerPads").toArray())m.towerPads.append(jsonPoint(v));
    if(m.bases.size()!=4||m.nodes.size()>80||m.towerPads.size()>80)return fail("Bad structure counts.");
    for(auto pt:m.bases)if(!m.validPoint(pt))return fail("Base is outside map.");
    for(auto node:m.nodes)if(!m.validPoint(node.tile)||node.type<0||node.type>=ResourceCount)return fail("Invalid resource node.");
    for(auto pt:m.towerPads)if(!m.validPoint(pt))return fail("Invalid tower pad.");
    out=std::move(m);return true;
}
bool ArenaMap::validate(int teams,QString *error) const {
    auto fail=[&](const QString &s){if(error)*error=s;return false;};
    if(teams!=2&&teams!=4)return fail("Only 2 or 4 teams are supported.");
    if(bases.size()!=4||terrain.size()!=width*height)return fail("Map is incomplete.");
    if(nodes.size()<teams*2)return fail(QString("At least %1 capture platforms are needed.").arg(teams*2));
    if(towerPads.size()<teams)return fail("At least one tower pad per team is required.");
    for(int team=0;team<teams;++team){
        bool nearBase=false;
        for(auto pad:towerPads){int dx=pad.x()-bases[team].x(),dy=pad.y()-bases[team].y();
            if(dx*dx+dy*dy<=81){nearBase=true;break;}}
        if(!nearBase)return fail(QString("Team %1 needs a tower pad within 9 tiles of its base.").arg(team+1));
    }
    QSet<int> occupied;
    auto claim=[&](QPoint p,const QString &type){
        if(!passable(p.x(),p.y())){if(error)*error=type+" must be on grass.";return false;}
        int key=p.y()*width+p.x();if(occupied.contains(key)){if(error)*error="Overlapping objects are not allowed.";return false;}
        occupied.insert(key);return true;
    };
    for(auto p:bases)if(!claim(p,"Base"))return false;
    for(auto n:nodes)if(!claim(n.tile,"Resource platform"))return false;
    for(auto p:towerPads)if(!claim(p,"Tower pad"))return false;
    // Map geometry rule: all active bases must be able to reach all capture platforms.
    for(int team=0;team<teams;++team){
        QVector<bool> reachable(width*height,false);QQueue<QPoint> q;
        q.enqueue(bases[team]);reachable[bases[team].y()*width+bases[team].x()]=true;
        while(!q.isEmpty()){
            QPoint p=q.dequeue();
            for(QPoint d:{QPoint(1,0),QPoint(-1,0),QPoint(0,1),QPoint(0,-1)}){
                QPoint a=p+d;int index=a.y()*width+a.x();
                if(passable(a.x(),a.y())&&!reachable[index]){reachable[index]=true;q.enqueue(a);}
            }
        }
        for(const auto &n:nodes)if(!reachable[n.tile.y()*width+n.tile.x()])
            return fail(QString("Team %1 cannot reach every resource platform.").arg(team+1));
        for(auto pad:towerPads)if(!reachable[pad.y()*width+pad.x()])
            return fail(QString("Team %1 cannot reach every tower pad.").arg(team+1));
    }
    return true;
}
ArenaMap ArenaMap::standard(){
    ArenaMap m;m.name="Frontier 96";m.width=96;m.height=64;
    m.terrain.fill(0,m.width*m.height);
    m.bases={{7,7},{88,56},{88,7},{7,56}};
    // Broken walls / lakes leave broad corridors between all starting areas.
    for(int island=0;island<14;++island){
        int cx=17+(island*37)%62,cy=11+(island*23)%42;
        int kind=(island%4==0)?2:1;
        for(int dy=-2;dy<=2;++dy)for(int dx=-3;dx<=3;++dx){
            if((dx*dx*4+dy*dy*9)>35)continue;
            int x=cx+dx,y=cy+dy;
            if(x<12||y<10||x>=84||y>=54)continue;
            // Leave a cross-shaped artery open, plus a buffer around spawn sectors.
            if(std::abs(x-48)<5||std::abs(y-32)<5)continue;
            m.terrain[y*m.width+x]=kind;
        }
    }
    // Place symmetrical resources across the large map, plus central objectives.
    for(int y=12;y<=52;y+=10)for(int x=14;x<=82;x+=12){
        if(m.nodes.size()>=64)break;
        QPoint pt(x+((y/10)%2)*2,y);int resource=(x/12+y/10)%ResourceCount;
        m.terrain[pt.y()*m.width+pt.x()]=0;
        m.nodes.append({pt,resource});
    }
    for(int team=0;team<4;++team){
        QPoint b=m.bases[team];
        for(QPoint delta:{QPoint(5,0),QPoint(0,5),QPoint(-5,0),QPoint(0,-5)}){
            QPoint pad=b+delta;
            if(m.validPoint(pad)&&!m.bases.contains(pad))m.towerPads.append(pad);
        }
    }
    // Open a 3x3 zone around all interactive markers for tank access.
    auto clear=[&](QPoint pt){for(int dy=-1;dy<=1;++dy)for(int dx=-1;dx<=1;++dx){
        int x=pt.x()+dx,y=pt.y()+dy;if(m.validPoint({x,y}))m.terrain[y*m.width+x]=0;
    }};
    for(QPoint pt:m.bases)clear(pt);
    for(const auto &n:m.nodes)clear(n.tile);
    for(QPoint pt:m.towerPads)clear(pt);
    return m;
}

QString mapDirectory(){QString dir=QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)+"/maps";QDir().mkpath(dir);return dir;}
QString cleanMapFilename(const QString &name){
    QString s=name.trimmed();s.replace(QRegularExpression("[^A-Za-z0-9_-]+"),"_");
    if(s.isEmpty())s="map";return s.left(50)+".json";
}
bool saveMap(const ArenaMap &m,const QString &path,QString *error){
    QFile f(path);if(!f.open(QIODevice::WriteOnly|QIODevice::Truncate)){if(error)*error=f.errorString();return false;}
    if(f.write(QJsonDocument(m.toJson()).toJson(QJsonDocument::Indented))<0){if(error)*error=f.errorString();return false;}
    return true;
}
bool loadMap(const QString &path,ArenaMap &m,QString *error){
    QFile f(path);if(!f.open(QIODevice::ReadOnly)){if(error)*error=f.errorString();return false;}
    if(f.size()>1024*1024){if(error)*error="Map file is too large.";return false;}
    QJsonParseError e;auto j=QJsonDocument::fromJson(f.readAll(),&e);
    if(e.error!=QJsonParseError::NoError||!j.isObject()){if(error)*error="Invalid JSON map.";return false;}
    return ArenaMap::fromJson(j.object(),m,error);
}
}
