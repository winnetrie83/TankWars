#pragma once
#include "map.h"
#include "world.h"
#include <QObject>
#include <QHash>
#include <QPointer>
#include <QTcpServer>
#include <QTcpSocket>
#include <QColor>
#include <QTimer>
#include <QElapsedTimer>

namespace tw {
class Session : public QObject {
    Q_OBJECT
public:
    explicit Session(QObject *parent=nullptr);
    ~Session() override;
    enum class Phase{Home,Lobby,Countdown,Loading,Playing,Ended};
    bool create(quint16 port,const QString &nickname);
    void join(const QString &address,quint16 port,const QString &nickname);
    void leave();
    bool isHost()const{return host_;}
    bool connected()const{return phase_!=Phase::Home;}
    Phase phase()const{return phase_;}
    int myId()const{return myId_;}
    int countdown()const{return countdown_;}
    QString name()const{return playerName_;}
    QString failure()const{return failure_;}
    const QVector<Participant> &players()const{return roster_;}
    const ArenaMap &map()const{return map_;}
    const MatchOptions &options()const{return options_;}
    QVector<QColor> teamColors() const;
    void configure(const MatchOptions &settings);
    void useMap(const ArenaMap &map);
    void addBot(const QString &strategy);
    void removeBot();
    void changeReady(bool ready);
    void changeTeam(int team);
    void changeVehicle(const QString &vehicle);
    void startMatch();
    void loaded();
    void sendInput(const Input &input);
    void purchase(const QString &kind,int pad=-1);
    void selectObject(const QString &type,int index=-1);
    void chooseRespawn(const QString &vehicle);
    void buildWallAt(QPointF position);
    QStringList waitingFor()const;
signals:
    void changed();
    void notice(const QString &text);
    void mapAvailable();
    void frame(const QJsonObject &snapshot);
private:
    struct Peer {int id=0;QByteArray buffer;};
    bool host_=false;int myId_=-1,nextId_=2,countdown_=0,frameNumber_=0;
    Phase phase_=Phase::Home;
    QString playerName_,failure_;
    MatchOptions options_;
    ArenaMap map_=ArenaMap::standard();
    QVector<Participant> roster_;
    QVector<QString> colors_={"#35a8ff","#ff6268","#79d38c","#f5be54"};
    QTcpServer *server_=nullptr;
    QTcpSocket *client_=nullptr;
    QByteArray clientBuffer_;
    QHash<QTcpSocket*,Peer> peers_;
    QByteArray receivingMap_,expectedHash_;
    int expectedSize_=0;
    QString downloadedMapName_;
    World world_;
    QTimer countdownTimer_,gameTimer_,loadingTimer_;
    QElapsedTimer simulationClock_;
    QHash<int,qint64> lastLoadingUpdate_;
    void send(QTcpSocket *socket,const QJsonObject &json);
    void broadcast(const QJsonObject &json);
    void sync();
    void setPhase(Phase phase);
    void resetReadiness();
    void distributeTeams();
    void removePeer(QTcpSocket *socket);
    void parseIncoming(QByteArray &buffer,QTcpSocket *socket,bool fromHost);
    void hostMessage(QTcpSocket *socket,const QJsonObject &message);
    void clientMessage(const QJsonObject &message);
    void transferMap(QTcpSocket *socket);
    void checkStartGame();
    void tickGame();
    Participant *find(int id);
};
}
