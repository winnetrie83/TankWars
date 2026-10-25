#include "sounds.h"
#include <QFile>
#include <QDir>
#include <QStandardPaths>
#include <QUrl>
#ifdef Q_OS_WIN
#include <windows.h>
#include <mmsystem.h>
#endif
namespace tw {
SoundManager::SoundManager(QObject *parent){
    QSettings s;enabled_=s.value("audio/enabled",true).toBool();
    volume_=s.value("audio/volume",65).toInt();
    const QStringList names={"shot","hit","blast","capture","click","jeep_mg","minigun","cannon","rocket","rocket_blast","mine_blast"};
#ifdef TW_HAVE_MULTIMEDIA
    for(const QString &name:names){
        auto *sound=new QSoundEffect(parent);
        sound->setSource(QUrl(QString("qrc:/audio/%1.wav").arg(name)));
        sound->setVolume(volume_/100.0);
        effects_[name]=sound;
    }
#elif defined(Q_OS_WIN)
    const QString location=QStandardPaths::writableLocation(QStandardPaths::CacheLocation)+"/sfx";
    QDir().mkpath(location);
    for(const QString &name:names){
        QFile original(QString(":/audio/%1.wav").arg(name));
        if(!original.open(QIODevice::ReadOnly))continue;
        const QString path=location+"/"+name+".wav";
        QFile target(path);
        if(target.open(QIODevice::WriteOnly|QIODevice::Truncate)){
            target.write(original.readAll());target.close();wavFiles_[name]=path;
        }
    }
#else
    Q_UNUSED(parent);Q_UNUSED(names);
#endif
}
bool SoundManager::available()const{
#if defined(TW_HAVE_MULTIMEDIA) || defined(Q_OS_WIN)
    return true;
#else
    return false;
#endif
}
bool SoundManager::volumeControlAvailable()const{
#ifdef TW_HAVE_MULTIMEDIA
    return true;
#else
    return false;
#endif
}
void SoundManager::setVolume(int value){
    volume_=qBound(0,value,100);QSettings().setValue("audio/volume",volume_);
#ifdef TW_HAVE_MULTIMEDIA
    for(auto *effect:effects_)effect->setVolume(volume_/100.0);
#endif
}
void SoundManager::play(const QString &name){
    if(!enabled_||volume_==0)return;
#ifdef TW_HAVE_MULTIMEDIA
    if(auto *sound=effects_.value(name,nullptr))sound->play();
#elif defined(Q_OS_WIN)
    const QString wav=wavFiles_.value(name);
    if(!wav.isEmpty())PlaySoundW(reinterpret_cast<LPCWSTR>(wav.utf16()),nullptr,SND_FILENAME|SND_ASYNC|SND_NODEFAULT);
#else
    Q_UNUSED(name);
#endif
}
}
