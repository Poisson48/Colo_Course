// tst_qml.cpp — Les écrans se chargent-ils sans erreur de binding, et la logique
// portée par le QML (sélection multiple, bouton retour) fait-elle ce qu'elle dit ?
//
// Les pages sont chargées depuis les sources (COLO_QML_DIR) avec les mêmes propriétés
// de contexte que main.cpp. Toute erreur QML — propriété absente, fonction manquante,
// binding cassé — remonte ici au lieu d'attendre le téléphone.

#include <QtTest>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QTemporaryDir>

#include "app/appcontroller.h"
#include "app/itemmodel.h"
#include "core/types.h"
#include "store/database.h"
#include "app/qrimageprovider.h"
#include "app/theme.h"
#include "app/updater.h"

// Les avertissements QML (binding cassé, appel sur undefined) ne remontent pas dans
// QQmlComponent::errors : ils passent par le gestionnaire de messages. On les capture.
static QStringList g_warnings;

static void warningCollector(QtMsgType type, const QMessageLogContext &ctx, const QString &msg)
{
    if (type == QtWarningMsg || type == QtCriticalMsg || type == QtFatalMsg)
        g_warnings << msg;
    Q_UNUSED(ctx);
}

class QmlTest : public QObject
{
    Q_OBJECT

private:
    QQmlEngine        *m_engine = nullptr;
    app::AppController m_ctrl;
    app::Theme         m_theme;
    // check() n'est pas appelé : le test ne doit pas dépendre du réseau. L'Updater
    // reste à l'état Idle, la bannière de mise à jour reste donc masquée.
    app::Updater       m_updater;

    // Tous les textes réellement posés à l'écran, en descendant l'arbre visuel. C'est
    // la seule façon de vérifier qu'une ligne affiche bien ses données, sans dépendre
    // de la structure interne des délégués.
    static QStringList visibleTexts(QQuickItem *root) {
        QStringList out;
        if (!root)
            return out;
        const QVariant text = root->property("text");
        if (text.isValid() && !text.toString().isEmpty())
            out << text.toString();
        for (QQuickItem *child : root->childItems())
            out += visibleTexts(child);
        return out;
    }

    // Instancie une page avec les propriétés de contexte de l'app réelle.
    QObject *load(const QString &file, const QVariantMap &props = {}) {
        QQmlComponent comp(m_engine,
                           QUrl::fromLocalFile(QStringLiteral(COLO_QML_DIR "/") + file));
        if (comp.status() != QQmlComponent::Ready) {
            qWarning() << "component not ready:" << comp.errorString();
            return nullptr;
        }
        return comp.createWithInitialProperties(props);
    }

private slots:
    void initTestCase() {
        // Sans le style Material, les attachements (Material.accent…) ne résolvent pas.
        QQuickStyle::setStyle(QStringLiteral("Material"));

        m_engine = new QQmlEngine(this);
        m_engine->addImageProvider(QStringLiteral("qr"), new app::QrImageProvider());
        m_engine->rootContext()->setContextProperty(QStringLiteral("AppController"), &m_ctrl);
        m_engine->rootContext()->setContextProperty(QStringLiteral("Theme"), &m_theme);
        m_engine->rootContext()->setContextProperty(QStringLiteral("Updater"), &m_updater);
    }

    // La comparaison de versions décide si l'app propose une mise à jour : une erreur
    // ici, et soit personne n'est prévenu, soit tout le monde est harcelé.
    void test_isNewer() {
        QVERIFY(app::Updater::isNewer("v0.5.0", "0.4.0"));
        QVERIFY(app::Updater::isNewer("0.4.1",  "0.4.0"));
        QVERIFY(app::Updater::isNewer("1.0.0",  "0.9.9"));
        // Le piège d'une comparaison de chaînes : "0.10.0" < "0.9.1" lexicographiquement.
        QVERIFY(app::Updater::isNewer("0.10.0", "0.9.1"));

        QVERIFY(!app::Updater::isNewer("0.4.0", "0.4.0"));
        QVERIFY(!app::Updater::isNewer("v0.4.0", "0.4.0"));   // même version, tag préfixé
        QVERIFY(!app::Updater::isNewer("0.3.9", "0.4.0"));
        QVERIFY(!app::Updater::isNewer("0.9.1", "0.10.0"));
        QVERIFY(!app::Updater::isNewer("",      "0.4.0"));    // release sans tag

        // Composants absents = 0 : "0.4" n'est pas plus récent que "0.4.0".
        QVERIFY(!app::Updater::isNewer("0.4",   "0.4.0"));
        QVERIFY(app::Updater::isNewer("0.4.1",  "0.4"));
    }

    // Les notes affichées avant l'installation : les nouveautés, et pas les consignes
    // d'installation qui suivent (on est déjà en train d'installer).
    void test_releaseNotes() {
        const QString body =
            "## Nouveautés\n"
            "Mode Courses.\n"
            "Rayons.\n"
            "\n"
            "---\n"
            "\n"
            "## Installation (Android, arm64)\n"
            "Téléchargez l'APK ci-dessous.\n";

        const QString notes = app::Updater::notesFromBody(body);

        QVERIFY(notes.contains(QStringLiteral("Mode Courses.")));
        QVERIFY(notes.contains(QStringLiteral("Rayons.")));
        // Coupé au séparateur.
        QVERIFY(!notes.contains(QStringLiteral("Installation")));
        QVERIFY(!notes.contains(QStringLiteral("APK")));
        // Le « ## » du Markdown n'a pas de rendu ici : il ne doit pas s'afficher.
        QVERIFY(!notes.contains(QLatin1Char('#')));
        QCOMPARE(notes.left(10), QStringLiteral("Nouveautés"));

        // Une release sans séparateur reste lisible en entier.
        QCOMPARE(app::Updater::notesFromBody("Juste un correctif."),
                 QStringLiteral("Juste un correctif."));
        QVERIFY(app::Updater::notesFromBody("").isEmpty());
    }

    void init() {
        g_warnings.clear();
    }

    // La fenêtre réelle : Main.qml, sa barre, son StackView, et l'écran des listes
    // qu'il instancie — dialogue du pseudo compris, puisqu'aucun nom n'est encore
    // choisi ici. C'est le chemin de démarrage d'une installation neuve.
    void test_mainWindowLoadsCleanly() {
        qInstallMessageHandler(warningCollector);
        QObject *window = load(QStringLiteral("Main.qml"));
        qInstallMessageHandler(nullptr);

        QVERIFY(window != nullptr);
        QVERIFY2(g_warnings.isEmpty(), qPrintable(g_warnings.join(QStringLiteral("\n"))));

        // Aucun nom choisi : l'écran d'accueil doit le demander plutôt que de laisser
        // « Moi » se diffuser aux autres participants.
        QVERIFY(!m_ctrl.hasDisplayName());

        delete window;
    }

    // Les lignes s'affichent-elles VRAIMENT ? Charger la page ne suffit pas à le dire :
    // sans articles, aucun délégué n'est instancié, et une ligne cassée passe inaperçue.
    // On ouvre donc une vraie liste, avec de vrais articles, dans la vraie fenêtre.
    void test_listPage_rendersRows() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());

        store::Database db;
        QVERIFY(db.open(dir.filePath("test.db")));

        core::ListMeta meta;
        meta.listId   = "list-1";
        meta.key      = std::vector<uint8_t>(32, 0x01);
        meta.title    = "Courses";
        meta.titleVer = { 1, "dev-A" };
        meta.lamport  = 1;
        meta.created  = 1000;
        QVERIFY(db.createList(meta));

        // Le modèle exposé à QML est celui de l'AppController : on le charge à la main,
        // sans init() (qui ouvrirait la base réelle et se connecterait aux relais).
        app::ItemModel *model = m_ctrl.items();
        model->load(db, "list-1", "dev-A");
        model->addItem("Lait", "1L");
        model->addItem("Papier toilette", "2", "6 couches épaisses", "Hygiène");
        QCOMPARE(model->count(), 2);

        qInstallMessageHandler(warningCollector);

        QObject *window = load(QStringLiteral("Main.qml"));
        QVERIFY(window);

        // La page s'empile comme dans l'app : par le signal qu'émet openList().
        QMetaObject::invokeMethod(&m_ctrl, "listOpened",
                                  Q_ARG(QString, "list-1"), Q_ARG(QString, "Courses"));

        // Laisser la vue créer ses délégués (elle le fait au rendu, pas à la poussée).
        QTest::qWait(200);
        qInstallMessageHandler(nullptr);

        QVERIFY2(g_warnings.isEmpty(), qPrintable(g_warnings.join(QStringLiteral("\n"))));

        // Le nom de chaque article doit être écrit quelque part à l'écran. S'il manque,
        // c'est que les lignes ne reçoivent plus leurs données du modèle.
        auto *win = qobject_cast<QQuickWindow *>(window);
        QVERIFY(win);
        const QStringList shown = visibleTexts(win->contentItem());
        QVERIFY2(shown.contains(QStringLiteral("Lait")),
                 qPrintable("textes affichés : " + shown.join(QStringLiteral(" | "))));
        QVERIFY(shown.contains(QStringLiteral("Papier toilette")));
        // Quantité et description viennent du même modèle : elles tombent avec le reste.
        QVERIFY(shown.contains(QStringLiteral("1L")));

        // Le menu des rayons ne se peuple qu'à l'ouverture : une liste vide ne se
        // verrait qu'au moment où l'on veut classer un article.
        QObject *aisleBox = window->findChild<QObject *>(QStringLiteral("addAisleBox"));
        QVERIFY2(aisleBox, "sélecteur de rayon introuvable");

        // Le popup du ComboBox est un objet à part : c'est lui qui s'ouvre.
        auto *aislePopup = aisleBox->property("popup").value<QObject *>();
        QVERIFY(aislePopup);

        qInstallMessageHandler(warningCollector);
        g_warnings.clear();
        QMetaObject::invokeMethod(aislePopup, "open");
        QTest::qWait(150);
        qInstallMessageHandler(nullptr);

        QVERIFY2(g_warnings.isEmpty(), qPrintable(g_warnings.join(QStringLiteral("\n"))));

        // Voir l'écran sans téléphone ni serveur X :
        //   QT_QPA_PLATFORM=offscreen QT_QUICK_BACKEND=software \
        //   COLO_SCREENSHOT=/tmp/ui.png ./tst_qml
        // Le rendu logiciel dessine hors écran ; c'est le seul moyen de vérifier ce qui
        // ne se voit pas (icônes manquantes, thème clair illisible) autrement qu'à l'œil.
        if (qEnvironmentVariableIsSet("COLO_SCREENSHOT"))
            win->grabWindow().save(qEnvironmentVariable("COLO_SCREENSHOT"));

        const QStringList options = visibleTexts(win->contentItem());
        QVERIFY2(options.contains(QStringLiteral("Crèmerie")),
                 qPrintable("rayons proposés : " + options.join(QStringLiteral(" | "))));
        QVERIFY(options.contains(QStringLiteral("Sans rayon")));

        delete window;
    }

    // ListPage se charge seule (elle n'ouvre aucun popup au chargement).
    void test_listPageLoadsCleanly() {
        qInstallMessageHandler(warningCollector);
        QObject *page = load(QStringLiteral("ListPage.qml"),
                             { {"listId", "list-1"}, {"listTitle", "Courses"} });
        qInstallMessageHandler(nullptr);

        QVERIFY(page != nullptr);
        QVERIFY2(g_warnings.isEmpty(), qPrintable(g_warnings.join(QStringLiteral("\n"))));
        delete page;
    }

    // Sélection multiple : la page compte ce qui est sélectionné et le dit dans le titre.
    void test_listPage_selection() {
        QObject *page = load(QStringLiteral("ListPage.qml"),
                             { {"listId", "list-1"}, {"listTitle", "Courses"} });
        QVERIFY(page);

        QCOMPARE(page->property("selectionMode").toBool(), false);
        QCOMPARE(page->property("pageTitle").toString(), QStringLiteral("Courses"));

        QMetaObject::invokeMethod(page, "toggleSelection", Q_ARG(QVariant, "item-a"));
        QCOMPARE(page->property("selectionMode").toBool(), true);
        QCOMPARE(page->property("pageTitle").toString(), QStringLiteral("1 sélectionné"));

        QMetaObject::invokeMethod(page, "toggleSelection", Q_ARG(QVariant, "item-b"));
        QCOMPARE(page->property("pageTitle").toString(), QStringLiteral("2 sélectionnés"));

        // Re-sélectionner le même article le désélectionne.
        QMetaObject::invokeMethod(page, "toggleSelection", Q_ARG(QVariant, "item-a"));
        QCOMPARE(page->property("pageTitle").toString(), QStringLiteral("1 sélectionné"));

        delete page;
    }

    // Le retour (bouton Android, flèche) sort d'abord de la sélection, et ne quitte
    // la page que s'il n'y a plus rien à annuler : c'est ce contrat que Main.qml lit.
    void test_listPage_handleBack() {
        QObject *page = load(QStringLiteral("ListPage.qml"),
                             { {"listId", "list-1"}, {"listTitle", "Courses"} });
        QVERIFY(page);

        QVariant handled;

        // Rien de sélectionné : le retour n'est pas absorbé → la page se dépile.
        QMetaObject::invokeMethod(page, "handleBack", Q_RETURN_ARG(QVariant, handled));
        QCOMPARE(handled.toBool(), false);

        // En sélection : le retour est absorbé et vide la sélection.
        QMetaObject::invokeMethod(page, "toggleSelection", Q_ARG(QVariant, "item-a"));
        QMetaObject::invokeMethod(page, "handleBack", Q_RETURN_ARG(QVariant, handled));
        QCOMPARE(handled.toBool(), true);
        QCOMPARE(page->property("selectionMode").toBool(), false);

        // La sélection vidée, le retour redevient un « quitter la page ».
        QMetaObject::invokeMethod(page, "handleBack", Q_RETURN_ARG(QVariant, handled));
        QCOMPARE(handled.toBool(), false);

        // Recherche ouverte : le retour la referme au lieu de quitter la liste.
        page->setProperty("searchOpen", true);
        QMetaObject::invokeMethod(page, "handleBack", Q_RETURN_ARG(QVariant, handled));
        QCOMPARE(handled.toBool(), true);
        QCOMPARE(page->property("searchOpen").toBool(), false);

        // Mode Courses : le retour en sort au lieu de quitter la liste.
        page->setProperty("shoppingMode", true);
        QMetaObject::invokeMethod(page, "handleBack", Q_RETURN_ARG(QVariant, handled));
        QCOMPARE(handled.toBool(), true);
        QCOMPARE(page->property("shoppingMode").toBool(), false);

        // La sélection passe avant le mode Courses : on défait le plus récent d'abord.
        page->setProperty("shoppingMode", true);
        QMetaObject::invokeMethod(page, "toggleSelection", Q_ARG(QVariant, "item-a"));
        QMetaObject::invokeMethod(page, "handleBack", Q_RETURN_ARG(QVariant, handled));
        QCOMPARE(handled.toBool(), true);
        QCOMPARE(page->property("selectionMode").toBool(), false);
        QCOMPARE(page->property("shoppingMode").toBool(), true);  // toujours en courses

        delete page;
    }
};

QTEST_MAIN(QmlTest)
#include "tst_qml.moc"
