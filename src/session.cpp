#include "session.h"
#include <QCryptographicHash>
#include <QDateTime>
#include <QHostAddress>
#include <utility>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QRandomGenerator>
#include <QSet>
#include <algorithm>
#include <cmath>

namespace tw {
Session::Session(QObject *parent):QObject(parent){
    countdownTimer_.setInterval(1000);
    connect(&countdownTimer_,&QTimer::timeout,this,[this]{
        if(phase_!=Phase::Countdown){countdownTimer_.stop();return;}
        if(--countdown_>0){sync();return;}
        countdownTimer_.stop();setPhase(Phase::Loading);
        for(auto &p:roster_){p.loaded=p.bot;p.progress=p.bot?100:0;p.status=p.bot?"AI ready":"Waiting to load map";}
        world_.start(map_,options_,roster_);
        qint64 now=QDateTime::currentMSecsSinceEpoch();
        lastLoadingUpdate_.clear();for(const auto &p:roster_)lastLoadingUpdate_[p.id]=now;
        loadingTimer_.start();sync();emit mapAvailable();
        // Clients receive map chunks only after receiving the Loading state.
        for(auto it=peers_.begin();it!=peers_.end();++it)transferMap(it.key());
        checkStartGame();
    });
    gameTimer_.setTimerType(Qt::PreciseTimer);
    gameTimer_.setInterval(33);
    connect(&gameTimer_,&QTimer::timeout,this,&Session::tickGame);
    loadingTimer_.setInterval(2000);
    connect(&loadingTimer_,&QTimer::timeout,this,[this]{
        if(!host_||phase_!=Phase::Loading){loadingTimer_.stop();return;}
        qint64 now=QDateTime::currentMSecsSinceEpoch();bool updated=false;
        for(auto &p:roster_)if(!p.bot&&!p.loaded&&now-lastLoadingUpdate_.value(p.id,now)>8000){
            QString text=p.progress==0?"Waiting for client/map download (slow connection?)":
                p.progress>=99?"Still verifying/loading game (or stalled)":"Map download slow/stalled";
            if(p.status!=text){p.status=text;updated=true;}
        }
        if(updated)sync();
    });
}
Session::~Session(){leave();}
QVector<QColor> Session::teamColors()const{QVector<QColor> c;for(const auto &s:colors_)c.append(QColor(s));return c;}
void Session::setPhase(Phase p){phase_=p;sync();}
Participant *Session::find(int id){for(auto &p:roster_)if(p.id==id)return &p;return nullptr;}
bool Session::create(quint16 port,const QString &nickname){
    leave();server_=new QTcpServer(this);
    if(!server_->listen(QHostAddress::AnyIPv4,port)){
        failure_=server_->errorString();delete server_;server_=nullptr;return false;
    }
    host_=true;myId_=1;nextId_=2;playerName_=nickname.trimmed().left(24);phase_=Phase::Lobby;
    Participant owner;owner.id=1;owner.team=0;owner.name=playerName_;owner.status="Not ready";roster_.append(owner);
    if(QRandomGenerator::global()->bounded(2))std::swap(colors_[0],colors_[1]);
    connect(server_,&QTcpServer::newConnection,this,[this]{
        while(server_&&server_->hasPendingConnections()){
            QTcpSocket *socket=server_->nextPendingConnection();
            socket->setSocketOption(QAbstractSocket::LowDelayOption,1);
            peers_.insert(socket,{});
            connect(socket,&QTcpSocket::readyRead,this,[this,socket]{
                if(!peers_.contains(socket))return;
                QByteArray incoming=peers_.value(socket).buffer+socket->readAll();
                parseIncoming(incoming,socket,false);
                if(peers_.contains(socket))peers_[socket].buffer=std::move(incoming);
            });
            connect(socket,&QTcpSocket::disconnected,this,[this,socket]{removePeer(socket);});
        }
    });
    emit changed();return true;
}
void Session::join(const QString &address,quint16 port,const QString &nickname){
    leave();host_=false;playerName_=nickname.trimmed().left(24);
    client_=new QTcpSocket(this);clientBuffer_.clear();phase_=Phase::Home;
    connect(client_,&QTcpSocket::connected,this,[this]{
        client_->setSocketOption(QAbstractSocket::LowDelayOption,1);
        send(client_,{{"type","hello"},{"version",ProtocolVersion},{"name",playerName_}});
    });
    connect(client_,&QTcpSocket::readyRead,this,[this]{
        clientBuffer_+=client_->readAll();parseIncoming(clientBuffer_,client_,true);
    });
    connect(client_,&QTcpSocket::errorOccurred,this,[this](QAbstractSocket::SocketError){
        if(client_){failure_=client_->errorString();emit notice("Connection error: "+failure_);emit changed();}
    });
    connect(client_,&QTcpSocket::disconnected,this,[this]{
        if(phase_!=Phase::Home){phase_=Phase::Home;emit notice("Disconnected from the host.");emit changed();}
    });
    client_->connectToHost(address,port);emit changed();
}
void Session::leave(){
    countdownTimer_.stop();gameTimer_.stop();loadingTimer_.stop();lastLoadingUpdate_.clear();
    // Shutdown without treating disconnect signals as player actions.
    if(server_){server_->close();server_->deleteLater();server_=nullptr;}
    for(auto socket:peers_.keys()){socket->disconnect(this);socket->abort();socket->deleteLater();}
    peers_.clear();
    if(client_){client_->disconnect(this);client_->abort();client_->deleteLater();client_=nullptr;}
    roster_.clear();clientBuffer_.clear();receivingMap_.clear();
    phase_=Phase::Home;host_=false;myId_=-1;nextId_=2;countdown_=0;failure_.clear();
    map_=ArenaMap::standard();options_=MatchOptions{};
}
void Session::send(QTcpSocket *socket,const QJsonObject &json){
    if(!socket||socket->state()!=QAbstractSocket::ConnectedState)return;
    const auto data=QJsonDocument(json).toJson(QJsonDocument::Compact);
    if(data.size()>128*1024)return;
    socket->write(data+'\n');
}
void Session::broadcast(const QJsonObject &message){
    for(auto socket:peers_.keys())send(socket,message);
}
void Session::sync(){
    if(host_){QJsonArray players;for(auto &p:roster_)players.append(p.json());
        QJsonArray teamColors;for(const auto &s:colors_)teamColors.append(s);
        broadcast({{"type","state"},{"phase",int(phase_)},{"countdown",countdown_},
                   {"players",players},{"options",options_.json()},{"mapName",map_.name},
                   {"colors",teamColors}});
    }
    emit changed();
}
void Session::parseIncoming(QByteArray &buffer,QTcpSocket *socket,bool fromHost){
    if(buffer.size()>256*1024){socket->disconnectFromHost();return;}
    int end=0;
    while((end=buffer.indexOf('\n'))>=0){
        QByteArray line=buffer.left(end);buffer.remove(0,end+1);
        if(line.size()>128*1024){socket->disconnectFromHost();return;}
        QJsonParseError e;QJsonDocument doc=QJsonDocument::fromJson(line,&e);
        if(e.error!=QJsonParseError::NoError||!doc.isObject())continue;
        if(fromHost)clientMessage(doc.object());else hostMessage(socket,doc.object());
        if(!socket||socket->state()!=QAbstractSocket::ConnectedState)break;
    }
}
void Session::resetReadiness(){
    for(auto &p:roster_)if(!p.bot){p.ready=false;p.loaded=false;p.status="Not ready";p.progress=0;}
    sync();
}
void Session::distributeTeams(){
    QVector<int> count(options_.teams,0);
    // Preserve human selections as much as possible.
    for(auto &p:roster_)if(!p.bot){p.team=std::clamp(p.team,0,options_.teams-1);++count[p.team];}
    for(auto &p:roster_)if(p.bot){auto i=std::min_element(count.begin(),count.end());p.team=int(i-count.begin());++(*i);}
}
void Session::configure(const MatchOptions &settings){
    if(!host_||phase_!=Phase::Lobby)return;
    options_=settings;distributeTeams();resetReadiness();
}
void Session::useMap(const ArenaMap &map){
    if(!host_||phase_!=Phase::Lobby)return;
    map_=map;resetReadiness();
}
void Session::addBot(const QString &strategy){
    if(!host_||phase_!=Phase::Lobby||roster_.size()>=8)return;
    static int botId=1000;
    Participant p;p.id=botId++;p.name="Bot "+QString::number(p.id-999);p.bot=true;
    p.strategy=(strategy=="Aggressive"||strategy=="Defender")?strategy:"Collector";
    p.vehicle=(botId%3==0?"Jeep":botId%3==1?"Tank":"Missile launcher");
    p.ready=true;p.loaded=true;p.status=p.strategy;
    roster_.append(p);distributeTeams();resetReadiness();
}
void Session::removeBot(){
    if(!host_||phase_!=Phase::Lobby)return;
    for(int k=roster_.size()-1;k>=0;--k)if(roster_[k].bot){roster_.removeAt(k);resetReadiness();return;}
}
void Session::changeReady(bool ready){
    if(phase_!=Phase::Lobby)return;
    if(host_){if(auto p=find(myId_)){p->ready=ready;p->status=ready?"Ready":"Not ready";sync();}}
    else send(client_,{{"type","ready"},{"ready",ready}});
}
void Session::changeTeam(int team){
    if(phase_!=Phase::Lobby||team<0||team>=options_.teams)return;
    if(host_){if(auto p=find(myId_)){p->team=team;distributeTeams();resetReadiness();}}
    else send(client_,{{"type","team"},{"team",team}});
}
void Session::changeVehicle(const QString &vehicle){
    if(phase_!=Phase::Lobby || (vehicle!="Jeep"&&vehicle!="Tank"&&vehicle!="Missile launcher"))return;
    if(host_){if(auto *p=find(myId_)){p->vehicle=vehicle;resetReadiness();}}
    else send(client_,{{"type","vehicle"},{"vehicle",vehicle}});
}
void Session::startMatch(){
    if(!host_||phase_!=Phase::Lobby||roster_.isEmpty())return;
    // Require an opposing team and all connected humans ready.
    QSet<int> represented;
    for(const auto &p:roster_){if(!p.bot&&!p.ready){emit notice("All human players must be ready.");return;}represented.insert(p.team);}
    if(represented.size()!=options_.teams){emit notice("Every team must have at least one player or bot.");return;}
    QString error;
    if(!map_.validate(options_.teams,&error)){emit notice("Invalid map: "+error);return;}
    countdown_=5;setPhase(Phase::Countdown);countdownTimer_.start();
}
void Session::transferMap(QTcpSocket *socket){
    if(!host_||!peers_.contains(socket))return;
    QByteArray data=QJsonDocument(map_.toJson()).toJson(QJsonDocument::Compact);
    QByteArray hash=QCryptographicHash::hash(data,QCryptographicHash::Sha256).toHex();
    send(socket,{{"type","map_begin"},{"name",map_.name},{"size",data.size()},{"hash",QString::fromLatin1(hash)}});
    for(int pos=0;pos<data.size();pos+=4096){
        QByteArray part=data.mid(pos,4096);
        send(socket,{{"type","map_chunk"},{"data",QString::fromLatin1(part.toBase64())}});
    }
    send(socket,{{"type","map_end"}});
}
void Session::loaded(){
    if(phase_!=Phase::Loading)return;
    if(host_){if(auto p=find(myId_)){p->loaded=true;p->progress=100;p->status="Loaded";sync();checkStartGame();}}
    else send(client_,{{"type","loaded"}});
}
void Session::checkStartGame(){
    if(!host_||phase_!=Phase::Loading)return;
    for(const auto &p:roster_)if(!p.bot&&!p.loaded)return;
    loadingTimer_.stop();setPhase(Phase::Playing);frameNumber_=0;simulationClock_.start();gameTimer_.start();
}
void Session::tickGame(){
    if(!host_||phase_!=Phase::Playing)return;
    double dt=simulationClock_.isValid()?simulationClock_.restart()/1000.0:0.033;
    world_.step(std::clamp(dt,0.0,0.08));
    if(++frameNumber_%2==0||world_.finished()){
        auto snapshot=world_.snapshot();
        // Cache each team's masked view once, even when several clients share the team.
        QHash<int,QJsonObject> perTeam;
        for(auto socket:peers_.keys()){
            const auto &peer=peers_.value(socket);
            auto player=std::find_if(roster_.cbegin(),roster_.cend(),[&](const Participant &p){return p.id==peer.id;});
            if(player!=roster_.cend()){
                if(!perTeam.contains(player->team))perTeam.insert(player->team,world_.snapshotForTeam(player->team,snapshot));
                send(socket,world_.decorateForPlayer(peer.id,perTeam.value(player->team)));
            }
        }
        const auto *mine=find(myId_);
        if(mine){
            if(!perTeam.contains(mine->team))perTeam.insert(mine->team,world_.snapshotForTeam(mine->team,snapshot));
            emit frame(world_.decorateForPlayer(myId_,perTeam.value(mine->team)));
        }else emit frame(snapshot);
    }
    if(world_.finished()){gameTimer_.stop();setPhase(Phase::Ended);}
}
void Session::sendInput(const Input &input){
    if(phase_!=Phase::Playing)return;
    if(host_)world_.setInput(myId_,input);
    else send(client_,{{"type","input"},{"input",input.json()}});
}
void Session::purchase(const QString &kind,int pad){
    if(phase_!=Phase::Playing)return;
    if(host_)world_.upgrade(myId_,kind,pad);
    else send(client_,{{"type","buy"},{"kind",kind},{"pad",pad}});
}
void Session::chooseRespawn(const QString &vehicle){
    if(phase_!=Phase::Playing)return;
    if(host_)world_.requestRespawn(myId_,vehicle);
    else send(client_,{{"type","respawn_vehicle"},{"vehicle",vehicle}});
}
void Session::buildWallAt(QPointF position){
    if(phase_!=Phase::Playing)return;
    if(host_)world_.buildWall(myId_,position);
    else send(client_,{{"type","build_wall"},{"x",position.x()},{"y",position.y()}});
}
void Session::selectObject(const QString &type,int index){
    if(phase_!=Phase::Playing)return;
    if(host_)world_.select(myId_,type,index);
    else send(client_,{{"type","select"},{"object",type},{"index",index}});
}
void Session::removePeer(QTcpSocket *socket){
    if(!peers_.contains(socket))return;
    int id=peers_.take(socket).id;lastLoadingUpdate_.remove(id);socket->deleteLater();
    if(!host_||id<=0)return;
    if(phase_==Phase::Playing||phase_==Phase::Ended){
        // An absent player does not freeze the match; an AI takes over their tank.
        if(auto *p=find(id)){p->name+=" (AI)";p->bot=true;p->strategy="Defender";p->status="Disconnected";}
        // The world retains this human-owned tank but receives no fresh input.
        world_.enableBotTakeover(id);
    }else{
        for(int n=0;n<roster_.size();++n)if(roster_[n].id==id){roster_.removeAt(n);break;}
        if(phase_==Phase::Lobby){distributeTeams();resetReadiness();}
        if(phase_==Phase::Loading)checkStartGame();
        if(phase_==Phase::Countdown){countdownTimer_.stop();phase_=Phase::Lobby;resetReadiness();}
    }
    sync();
}
QStringList Session::waitingFor()const{
    QStringList list;
    if(phase_==Phase::Loading)for(const auto &p:roster_)if(!p.bot&&!p.loaded)
        list<<QString("%1 — %2 (%3%)").arg(p.name,p.status).arg(p.progress);
    return list;
}
void Session::hostMessage(QTcpSocket *socket,const QJsonObject &j){
    if(!host_||!peers_.contains(socket))return;
    const QString type=j.value("type").toString();Peer &peer=peers_[socket];
    if(peer.id==0){
        if(type!="hello")return;
        if(phase_!=Phase::Lobby||roster_.size()>=8||j.value("version").toInt()!=ProtocolVersion){
            send(socket,{{"type","reject"},{"reason","Lobby closed, full, or incompatible game version."}});
            socket->disconnectFromHost();return;
        }
        QString nickname=j.value("name").toString().trimmed().left(24);
        if(nickname.isEmpty())nickname="Player";
        peer.id=nextId_++;
        Participant p;p.id=peer.id;p.name=nickname;p.status="Not ready";
        roster_.append(p);distributeTeams();resetReadiness();
        send(socket,{{"type","welcome"},{"id",peer.id}});sync();return;
    }
    auto *p=find(peer.id);if(!p)return;
    if(type=="ready"&&phase_==Phase::Lobby){p->ready=j.value("ready").toBool();p->status=p->ready?"Ready":"Not ready";sync();}
    else if(type=="team"&&phase_==Phase::Lobby){int t=j.value("team").toInt(-1);
        if(t>=0&&t<options_.teams){p->team=t;distributeTeams();resetReadiness();}}
    else if(type=="progress"&&phase_==Phase::Loading){
        lastLoadingUpdate_[peer.id]=QDateTime::currentMSecsSinceEpoch();
        p->progress=std::clamp(j.value("percent").toInt(),0,99);
        p->status=p->progress>=99?"Verifying map / initializing":"Downloading map";sync();
    }else if(type=="loaded"&&phase_==Phase::Loading){lastLoadingUpdate_[peer.id]=QDateTime::currentMSecsSinceEpoch();p->loaded=true;p->progress=100;p->status="Loaded";sync();checkStartGame();}
    else if(type=="load_error"&&phase_==Phase::Loading){lastLoadingUpdate_[peer.id]=QDateTime::currentMSecsSinceEpoch();p->status=j.value("reason").toString().left(100);sync();}
    else if(type=="vehicle"&&phase_==Phase::Lobby){
        QString v=j.value("vehicle").toString();if(v=="Jeep"||v=="Tank"||v=="Missile launcher"){p->vehicle=v;resetReadiness();}}
    else if(type=="select"&&phase_==Phase::Playing)world_.select(peer.id,j.value("object").toString(),j.value("index").toInt(-1));
    else if(type=="input"&&phase_==Phase::Playing)world_.setInput(peer.id,Input::fromJson(j.value("input").toObject()));
    else if(type=="buy"&&phase_==Phase::Playing)world_.upgrade(peer.id,j.value("kind").toString(),j.value("pad").toInt(-1));
    else if(type=="respawn_vehicle"&&phase_==Phase::Playing)world_.requestRespawn(peer.id,j.value("vehicle").toString());
    else if(type=="build_wall"&&phase_==Phase::Playing){
        const double x=j.value("x").toDouble(-1e6),y=j.value("y").toDouble(-1e6);
        if(std::isfinite(x)&&std::isfinite(y))world_.buildWall(peer.id,{x,y});
    }
}
void Session::clientMessage(const QJsonObject &j){
    QString type=j.value("type").toString();
    if(type=="reject"){emit notice(j.value("reason").toString());if(client_)client_->disconnectFromHost();}
    else if(type=="welcome"){myId_=j.value("id").toInt();phase_=Phase::Lobby;emit changed();}
    else if(type=="state"){
        phase_=static_cast<Phase>(std::clamp(j.value("phase").toInt(),0,5));
        countdown_=j.value("countdown").toInt();options_=MatchOptions::fromJson(j.value("options").toObject());
        roster_.clear();for(const auto &v:j.value("players").toArray())roster_.append(Participant::fromJson(v.toObject()));
        colors_.clear();for(const auto &v:j.value("colors").toArray())colors_.append(v.toString());
        if(phase_==Phase::Lobby)map_.name=j.value("mapName").toString("Crossroads");
        emit changed();
    }else if(type=="map_begin"){
        receivingMap_.clear();expectedSize_=j.value("size").toInt();
        downloadedMapName_=j.value("name").toString().left(60);
        expectedHash_=j.value("hash").toString().toLatin1();
        if(expectedSize_<=0||expectedSize_>1024*1024){
            send(client_,{{"type","load_error"},{"reason","Invalid map transfer size"}});
            if(client_)client_->disconnectFromHost();return;
        }
        emit changed();
    }else if(type=="map_chunk"){
        QByteArray chunk=QByteArray::fromBase64(j.value("data").toString().toLatin1());
        if(receivingMap_.size()+chunk.size()>expectedSize_){
            send(client_,{{"type","load_error"},{"reason","Map transfer overflow"}});
            if(client_)client_->disconnectFromHost();return;
        }
        receivingMap_.append(chunk);
        int progress=std::min(99,int(100.0*receivingMap_.size()/std::max(1,expectedSize_)));
        send(client_,{{"type","progress"},{"percent",progress}});
        emit changed();
    }else if(type=="map_end"){
        QByteArray actual=QCryptographicHash::hash(receivingMap_,QCryptographicHash::Sha256).toHex();
        ArenaMap received;QString error;
        if(receivingMap_.size()!=expectedSize_||actual!=expectedHash_||
           !ArenaMap::fromJson(QJsonDocument::fromJson(receivingMap_).object(),received,&error)||
           !received.validate(options_.teams,&error)){
            emit notice("Map download failed: "+(error.isEmpty()?"checksum mismatch":error));
            send(client_,{{"type","load_error"},{"reason",error.isEmpty()?"Checksum mismatch":error}});return;
        }
        map_=received;
        // Keep an automatic local copy. It is never executed, only interpreted as map data.
        saveMap(map_,mapDirectory()+"/downloaded_"+cleanMapFilename(map_.name));
        receivingMap_.clear();emit mapAvailable();emit changed();
    }else if(type=="snapshot")emit frame(j);
}
}
