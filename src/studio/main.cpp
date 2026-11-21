#include "main_window.h"

#include <QApplication>
#include <QLocale>
#include <QSettings>
#include <QStyleFactory>
#include <QTranslator>

int main(int argc, char* argv[]) {
    QApplication app(argc, argv);
    QApplication::setOrganizationName(QStringLiteral("Daniel Nunes"));
    QApplication::setApplicationName(QStringLiteral("VTHOST Studio"));
    QApplication::setApplicationVersion(QStringLiteral(VTHOST_VERSION));
    QApplication::setStyle(QStyleFactory::create(QStringLiteral("Fusion")));

    // Idioma: escolha guardada, senão o do sistema (português -> pt_PT, resto -> inglês).
    QString lang = QSettings().value(QStringLiteral("language")).toString();
    if (lang.isEmpty())
        lang = QLocale::system().language() == QLocale::Portuguese ? QStringLiteral("pt_PT") : QStringLiteral("en");
    QTranslator translator;
    if (lang == QStringLiteral("pt_PT") && translator.load(QStringLiteral(":/i18n/vthost_studio_pt_PT.qm")))
        QApplication::installTranslator(&translator);

    MainWindow w;
    w.show();
    return QApplication::exec();
}
