#include "main_window.h"

#include <QApplication>
#include <QIcon>
#include <QLocale>
#include <QPalette>
#include <QProcess>
#include <QSettings>
#include <QStyleFactory>
#include <QTranslator>

int main(int argc, char* argv[]) {
    QApplication app(argc, argv);
    QApplication::setOrganizationName(QStringLiteral("Daniel Nunes"));
    // Nome interno mantém-se (as definições guardadas ficam em "VTHOST Studio");
    // o nome visível é VIDEOHOST PLAYER.
    QApplication::setApplicationName(QStringLiteral("VTHOST Studio"));
    QApplication::setApplicationDisplayName(QStringLiteral("VIDEOHOST PLAYER"));
    QApplication::setApplicationVersion(QStringLiteral(VTHOST_VERSION));
    QApplication::setWindowIcon(QIcon(QStringLiteral(":/branding/vthost-icon-256.png")));
    QApplication::setStyle(QStyleFactory::create(QStringLiteral("Fusion")));

    // Tema escuro (salas de espetáculo são escuras; laranja como cor de destaque).
    QPalette pal;
    const QColor window(45, 45, 45), base(32, 32, 32), text(222, 222, 222), accent(255, 136, 0);
    pal.setColor(QPalette::Window, window);
    pal.setColor(QPalette::WindowText, text);
    pal.setColor(QPalette::Base, base);
    pal.setColor(QPalette::AlternateBase, window);
    pal.setColor(QPalette::ToolTipBase, QColor(20, 20, 20));
    pal.setColor(QPalette::ToolTipText, text);
    pal.setColor(QPalette::Text, text);
    pal.setColor(QPalette::Button, QColor(58, 58, 58));
    pal.setColor(QPalette::ButtonText, text);
    pal.setColor(QPalette::BrightText, Qt::red);
    pal.setColor(QPalette::Highlight, accent);
    pal.setColor(QPalette::HighlightedText, Qt::black);
    pal.setColor(QPalette::Link, accent);
    pal.setColor(QPalette::PlaceholderText, QColor(130, 130, 130));
    pal.setColor(QPalette::Disabled, QPalette::Text, QColor(120, 120, 120));
    pal.setColor(QPalette::Disabled, QPalette::ButtonText, QColor(120, 120, 120));
    pal.setColor(QPalette::Disabled, QPalette::WindowText, QColor(120, 120, 120));
    QApplication::setPalette(pal);

    // Idioma: escolha guardada, senão o do sistema (português -> pt_PT, resto -> inglês).
    QString lang = QSettings().value(QStringLiteral("language")).toString();
    if (lang.isEmpty())
        lang = QLocale::system().language() == QLocale::Portuguese ? QStringLiteral("pt_PT") : QStringLiteral("en");
    QTranslator translator;
    if (lang == QStringLiteral("pt_PT") && translator.load(QStringLiteral(":/i18n/vthost_studio_pt_PT.qm")))
        QApplication::installTranslator(&translator);

    int rc = 0;
    {
        MainWindow w;
        w.show();
        rc = QApplication::exec();
    }
    // Definições → língua → "reabrir já": abre outra vez (o motor nunca parou).
    if (qApp->property("vthostRestart").toBool()) QProcess::startDetached(QCoreApplication::applicationFilePath(), {});
    return rc;
}
