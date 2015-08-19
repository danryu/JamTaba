#include "gui/mainframe.h"
#include "MainController.h"
#include "JamtabaFactory.h"
#include "persistence/Settings.h"
#include <QTextCodec>
#include <QLoggingCategory>
#include <QDir>
#include <QFile>

//#include "../audio/vorbis/VorbisEncoder.h"
//#include "../audio/core/AudioNode.h"
//#include "../audio/Resampler.h"
//#include "../audio/SamplesBufferResampler.h"
//#include "../audio/core/SamplesBuffer.h"

void customLogHandler(QtMsgType, const QMessageLogContext &, const QString &);

#include "audio/vorbis/VorbisDecoder.h"

int main(int argc, char* args[] ){

    if (!QTextCodec::codecForLocale()) {
      QTextCodec::setCodecForLocale(QTextCodec::codecForName("UTF-8"));
    }

    QApplication::setApplicationName("Jamtaba 2");

    qputenv("QT_LOGGING_CONF", ":/qtlogging.ini");//log cconfigurations is in resources at moment

    qInstallMessageHandler(customLogHandler);

    JamtabaFactory* factory = new ReleaseFactory();
    Persistence::Settings settings;//read from file in constructor
    settings.load();

    Controller::MainController mainController(factory, settings, argc, args);//MainController extends QApplication

    MainFrame mainFrame(&mainController);
    mainFrame.show();

    delete factory;
    int returnCode = mainController.exec();
    mainController.saveLastUserSettings(mainFrame.getInputsSettings());
    return returnCode;
 }

//++++++++++++++++++++++++++++++++++

void customLogHandler(QtMsgType type, const QMessageLogContext &context, const QString &msg)
{
    QByteArray localMsg = msg.toLocal8Bit();
    QString stringMsg;
    QString fullFileName(context.file);
    const char* file;
    int lastPathSeparatorIndex = fullFileName.lastIndexOf(QDir::separator());
    if(lastPathSeparatorIndex){
        file = fullFileName.right(fullFileName.size() - lastPathSeparatorIndex - 1).toStdString().c_str();
    }
    else{
        file = fullFileName.toStdString().c_str();
    }

    switch (type) {
    case QtDebugMsg:
        stringMsg = QString::asprintf("\nDEBUG: %s [%s: %u]", localMsg.constData(), file , context.line);
        break;
    case QtWarningMsg:
        stringMsg = QString::asprintf("\n\nWARNING: %s (%s) [%s:%u]\n", localMsg.constData(), context.function, file, context.line);
        break;
    case QtCriticalMsg:
        stringMsg = QString::asprintf("\n\nCRITICAL: %s (%s) [%s:%u]\n\n", localMsg.constData(), context.function, file, context.line);
        break;
    case QtFatalMsg:
        stringMsg = QString::asprintf("\n\nFATAL: %s (%s) [%s:%u]\n\n", localMsg.constData(), context.function, file, context.line);
        //abort();
        break;
    case QtInfoMsg:
        stringMsg = QString::asprintf("\n\nINFO: %s [%s:%u]\n\n", localMsg.constData(), file, context.line);
    }
    fprintf(stderr, stringMsg.toStdString().c_str());//write log message to console
    fflush(stderr);

    if(type != QtDebugMsg){//write the critical messages to log file
        QFile outFile("log.txt");
        outFile.open(QIODevice::WriteOnly | QIODevice::Append);
        QTextStream ts(&outFile);
        ts << stringMsg << "\n\r";
    }

    if(type == QtFatalMsg){
        abort();
    }
}
