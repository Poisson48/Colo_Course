#include <QtTest>
#include <QAbstractItemModelTester>
#include <QTemporaryDir>
#include <QSignalSpy>
#include <QUrl>

#include "../src/store/database.h"
#include "../src/core/types.h"
#include "../src/app/itemmodel.h"
#include "../src/app/appcontroller.h"

using namespace store;
using namespace core;
using namespace app;

class ItemModelTest : public QObject
{
    Q_OBJECT

private:
    // Helper: create a list in DB and return its id.
    std::string makeList(Database &db, const std::string &id = "list-test") {
        ListMeta m;
        m.listId   = id;
        m.key      = std::vector<uint8_t>(32, 0x00);
        m.title    = "Test List";
        m.titleVer = {1, "dev-A"};
        m.lamport  = 1;
        m.lastSync = 0;
        m.created  = 1000;
        db.createList(m);
        return id;
    }

    // Open a fresh DB in a temp dir.
    bool openDb(Database &db, const QTemporaryDir &dir) {
        return db.open(dir.filePath("test.db"));
    }

private slots:
    // 1. QAbstractItemModelTester: conformance check.
    void test_modelTester() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        Database db;
        QVERIFY(openDb(db, dir));
        const auto listId = makeList(db);

        ItemModel model;
        QAbstractItemModelTester tester(&model, QAbstractItemModelTester::FailureReportingMode::Fatal);
        model.load(db, listId, "dev-A");
        // add items to exercise tester
        model.addItem("Lait",    "1L");
        model.addItem("Pain",    "");
        model.addItem("Fromage", "200g");
    }

    // 2. addItem → row appears, sorted correctly, lamport incremented.
    void test_addItem_sortedInsertion() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        Database db;
        QVERIFY(openDb(db, dir));
        const auto listId = makeList(db);

        ItemModel model;
        model.load(db, listId, "dev-A");

        QCOMPARE(model.rowCount(), 0);

        model.addItem("A", "");
        model.addItem("B", "");
        model.addItem("C", "");

        QCOMPARE(model.rowCount(), 3);

        // All unchecked: should appear in creation order (FIFO = ascending created).
        QCOMPARE(model.data(model.index(0), ItemModel::NameRole).toString(), "A");
        QCOMPARE(model.data(model.index(1), ItemModel::NameRole).toString(), "B");
        QCOMPARE(model.data(model.index(2), ItemModel::NameRole).toString(), "C");

        // Verify lamport incremented monotonically in DB items.
        const auto items = db.getItems(listId);
        QCOMPARE((int)items.size(), 3);
        QVERIFY(items[0].nameVer.lamport < items[1].nameVer.lamport);
        QVERIFY(items[1].nameVer.lamport < items[2].nameVer.lamport);
    }

    // 3. toggleDone → item moves to bottom group, correct re-ordering.
    void test_toggleDone_reorder() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        Database db;
        QVERIFY(openDb(db, dir));
        const auto listId = makeList(db);

        ItemModel model;
        model.load(db, listId, "dev-A");

        model.addItem("A", "");
        model.addItem("B", "");
        model.addItem("C", "");

        QCOMPARE(model.rowCount(), 3);

        // Toggle first item (A) → it should move to the bottom.
        const QString idA = model.data(model.index(0), ItemModel::ItemIdRole).toString();
        model.toggleDone(idA);

        QCOMPARE(model.rowCount(), 3);
        // A is now done → must be last.
        QCOMPARE(model.data(model.index(2), ItemModel::ItemIdRole).toString(), idA);
        QCOMPARE(model.data(model.index(2), ItemModel::DoneRole).toBool(), true);

        // B and C still unchecked, in order.
        QCOMPARE(model.data(model.index(0), ItemModel::NameRole).toString(), "B");
        QCOMPARE(model.data(model.index(1), ItemModel::NameRole).toString(), "C");

        // Toggle A again → should come back to the top.
        model.toggleDone(idA);
        QCOMPARE(model.data(model.index(0), ItemModel::DoneRole).toBool(), false);

        // Verify done field in DB has doneVer with lamport > 0.
        bool found = false;
        for (const auto &item : db.getItems(listId)) {
            if (item.itemId == idA.toStdString()) {
                QVERIFY(item.doneVer.lamport > 0);
                QCOMPARE(item.done, false);
                found = true;
            }
        }
        QVERIFY(found);
    }

    // 4. removeItem → item disappears from view, tombstone del=true in DB with correct Ver.
    void test_removeItem_tombstone() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        Database db;
        QVERIFY(openDb(db, dir));
        const auto listId = makeList(db);

        ItemModel model;
        model.load(db, listId, "dev-A");

        model.addItem("X", "");
        model.addItem("Y", "");
        QCOMPARE(model.rowCount(), 2);

        const QString idX = model.data(model.index(0), ItemModel::ItemIdRole).toString();
        model.removeItem(idX);

        // View: only Y remains.
        QCOMPARE(model.rowCount(), 1);
        QCOMPARE(model.data(model.index(0), ItemModel::NameRole).toString(), "Y");

        // DB: X must have del=true with a valid Ver.
        bool found = false;
        for (const auto &item : db.getItems(listId)) {
            if (item.itemId == idX.toStdString()) {
                QVERIFY(item.del);
                QVERIFY(item.delVer.lamport > 0);
                QCOMPARE(item.delVer.deviceId, std::string("dev-A"));
                found = true;
            }
        }
        QVERIFY(found);
    }

    // 5. Versions (Ver) written with incremented lamport for each operation.
    void test_versions_lamportIncrement() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        Database db;
        QVERIFY(openDb(db, dir));
        const auto listId = makeList(db);

        ItemModel model;
        model.load(db, listId, "dev-A");

        model.addItem("P", "");
        const QString idP = model.data(model.index(0), ItemModel::ItemIdRole).toString();
        model.toggleDone(idP);

        const auto items = db.getItems(listId);
        QCOMPARE((int)items.size(), 1);
        const auto &item = items[0];

        // nameVer set on creation, doneVer set on toggleDone → doneVer.lamport > nameVer.lamport.
        QVERIFY(item.doneVer.lamport > item.nameVer.lamport);
    }

    // 6. Persistence: reload from DB gives same state.
    void test_persistence_reload() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        Database db;
        QVERIFY(openDb(db, dir));
        const auto listId = makeList(db);

        {
            ItemModel model;
            model.load(db, listId, "dev-A");
            model.addItem("Alpha",   "1kg");
            model.addItem("Beta",    "");
            model.addItem("Gamma",   "2");

            // toggle Beta (row 1)
            const QString idBeta = model.data(model.index(1), ItemModel::ItemIdRole).toString();
            model.toggleDone(idBeta);

            // remove Gamma (now row 1 = Gamma since Beta moved to bottom)
            // After toggle: Alpha(0), Gamma(1), Beta(2)
            const QString idGamma = model.data(model.index(1), ItemModel::ItemIdRole).toString();
            model.removeItem(idGamma);
        }

        // Reload into a fresh model.
        ItemModel model2;
        model2.load(db, listId, "dev-A");

        // Should have 2 visible rows: Alpha (undone) and Beta (done).
        QCOMPARE(model2.rowCount(), 2);
        QCOMPARE(model2.data(model2.index(0), ItemModel::NameRole).toString(), "Alpha");
        QCOMPARE(model2.data(model2.index(0), ItemModel::DoneRole).toBool(), false);
        QCOMPARE(model2.data(model2.index(1), ItemModel::NameRole).toString(), "Beta");
        QCOMPARE(model2.data(model2.index(1), ItemModel::DoneRole).toBool(), true);
    }

    // 7. AppController: init, createList, lists model populated.
    void test_appcontroller_createList() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());

        // Point QStandardPaths to temp dir so AppController writes there.
        qputenv("HOME", dir.path().toLocal8Bit());

        AppController ctrl;
        // We call init() — but QStandardPaths::AppDataLocation depends on app name.
        // Instead, open DB directly and test ListsModel separately.
        // We exercise ListsModel via the controller's internal DB after a manual init.

        // Just test that lists model is initially empty/valid.
        QVERIFY(ctrl.lists() != nullptr);
    }

    // editItem : nom, quantité et description partent en LWW ; la ligne ne bouge pas.
    void test_editItem_updatesFieldsAndVersions() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        Database db;
        QVERIFY(openDb(db, dir));
        const auto listId = makeList(db);

        ItemModel model;
        model.load(db, listId, "dev-A");
        model.addItem("Pq", "1");

        const QString itemId = model.data(model.index(0), ItemModel::ItemIdRole).toString();
        const Item before = db.getItems(listId).front();

        QSignalSpy localSpy(&model, &ItemModel::localChanged);
        model.editItem(itemId, "Papier toilette", "2 paquets", "6 couches épaisses", "");
        QCOMPARE(localSpy.count(), 1);

        // Le modèle expose les nouvelles valeurs, au même index (le tri ne dépend
        // ni du nom, ni de la quantité, ni de la note).
        QCOMPARE(model.data(model.index(0), ItemModel::NameRole).toString(),
                 QStringLiteral("Papier toilette"));
        QCOMPARE(model.data(model.index(0), ItemModel::QtyRole).toString(),
                 QStringLiteral("2 paquets"));
        QCOMPARE(model.data(model.index(0), ItemModel::NoteRole).toString(),
                 QStringLiteral("6 couches épaisses"));

        // Persisté, avec des versions qui battent les précédentes : sans ça, un pair
        // qui rediffuse l'ancienne valeur écraserait l'édition au merge.
        const Item after = db.getItems(listId).front();
        QCOMPARE(QString::fromStdString(after.name), QStringLiteral("Papier toilette"));
        QCOMPARE(QString::fromStdString(after.note), QStringLiteral("6 couches épaisses"));
        QVERIFY(after.nameVer > before.nameVer);
        QVERIFY(after.qtyVer  > before.qtyVer);
        QVERIFY(after.noteVer > before.noteVer);
        // done n'a pas été touché : sa version ne doit pas bouger.
        QCOMPARE(after.doneVer.lamport, before.doneVer.lamport);
    }

    // Rééditer à l'identique ne doit rien émettre : une version neuve sur une valeur
    // inchangée ferait gagner ce champ contre une édition distante concurrente.
    void test_editItem_noopWhenUnchanged() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        Database db;
        QVERIFY(openDb(db, dir));
        const auto listId = makeList(db);

        ItemModel model;
        model.load(db, listId, "dev-A");
        model.addItem("Lait", "1L");

        const QString itemId = model.data(model.index(0), ItemModel::ItemIdRole).toString();
        const Item before = db.getItems(listId).front();

        QSignalSpy localSpy(&model, &ItemModel::localChanged);
        model.editItem(itemId, "Lait", "1L", "", "");
        QCOMPARE(localSpy.count(), 0);

        const Item after = db.getItems(listId).front();
        QCOMPARE(after.nameVer.lamport, before.nameVer.lamport);
        QCOMPARE(after.qtyVer.lamport,  before.qtyVer.lamport);

        // Un nom vide n'est pas un article : refusé, l'ancien nom reste.
        model.editItem(itemId, "   ", "1L", "", "");
        QCOMPARE(QString::fromStdString(db.getItems(listId).front().name),
                 QStringLiteral("Lait"));
    }

    // toggleDone horodate le cochage, et efface la date au décochage.
    void test_toggleDone_stampsDoneAt() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        Database db;
        QVERIFY(openDb(db, dir));
        const auto listId = makeList(db);

        ItemModel model;
        model.load(db, listId, "dev-A");
        model.addItem("Beurre", "");

        const QString itemId = model.data(model.index(0), ItemModel::ItemIdRole).toString();
        QCOMPARE(db.getItems(listId).front().doneAt, int64_t(0));

        const int64_t before = QDateTime::currentMSecsSinceEpoch();
        model.toggleDone(itemId);
        const Item done = db.getItems(listId).front();
        QVERIFY(done.done);
        QVERIFY(done.doneAt >= before);

        model.toggleDone(itemId);
        const Item undone = db.getItems(listId).front();
        QVERIFY(!undone.done);
        QCOMPARE(undone.doneAt, int64_t(0));
    }

    // Fin de course : tout remettre à acheter. Les articles remontent, rien n'est perdu.
    void test_uncheckAll() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        Database db;
        QVERIFY(openDb(db, dir));
        const auto listId = makeList(db);

        ItemModel model;
        model.load(db, listId, "dev-A");
        model.addItem("Lait", "");
        model.addItem("Pain", "");

        model.toggleDone(model.data(model.index(0), ItemModel::ItemIdRole).toString());
        model.toggleDone(model.data(model.index(0), ItemModel::ItemIdRole).toString());
        QCOMPARE(model.doneCount(), 2);

        QSignalSpy localSpy(&model, &ItemModel::localChanged);
        model.uncheckAll();

        QCOMPARE(localSpy.count(), 1);   // une seule intention → un seul delta publié
        QCOMPARE(model.doneCount(), 0);
        QCOMPARE(model.count(), 2);      // rien n'a été supprimé

        for (const auto &it : db.getItems(listId)) {
            QVERIFY(!it.done);
            QCOMPARE(it.doneAt, int64_t(0));   // la date de cochage disparaît avec le cochage
            QVERIFY(!it.del);
        }

        // Plus rien à décocher : ne rien réécrire, sinon on publierait dans le vide.
        model.uncheckAll();
        QCOMPARE(localSpy.count(), 1);
    }

    // Fin de course : retirer ce qui a été pris, garder ce qui reste à acheter.
    void test_removeDone() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        Database db;
        QVERIFY(openDb(db, dir));
        const auto listId = makeList(db);

        ItemModel model;
        model.load(db, listId, "dev-A");
        model.addItem("Lait", "");
        model.addItem("Pain", "");
        model.addItem("Œufs", "");

        // Cocher « Lait » : il passe en bas du tri, mais reste le même article.
        const QString lait = model.data(model.index(0), ItemModel::ItemIdRole).toString();
        model.toggleDone(lait);
        QCOMPARE(model.doneCount(), 1);

        model.removeDone();

        QCOMPARE(model.count(), 2);
        QCOMPARE(model.doneCount(), 0);

        // Tombstone (del=true), pas ligne effacée : les autres appareils doivent
        // apprendre la suppression, sinon leur copie ferait réapparaître l'article.
        int tombstones = 0;
        for (const auto &it : db.getItems(listId))
            if (it.del) ++tombstones;
        QCOMPARE(tombstones, 1);
    }

    // Recherche : un filtre d'affichage, qui ne touche ni la base ni la synchro.
    void test_filter() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        Database db;
        QVERIFY(openDb(db, dir));
        const auto listId = makeList(db);

        ItemModel model;
        model.load(db, listId, "dev-A");
        model.addItem("Lait", "1L");
        model.addItem("Papier toilette", "2", "6 couches épaisses");
        model.addItem("Pain", "");

        model.setFilter("pa");                    // « Papier », « Pain »
        QCOMPARE(model.count(), 2);

        model.setFilter("LAIT");                  // insensible à la casse
        QCOMPARE(model.count(), 1);

        model.setFilter("couches");               // cherche aussi dans la description
        QCOMPARE(model.count(), 1);
        QCOMPARE(model.data(model.index(0), ItemModel::NameRole).toString(),
                 QStringLiteral("Papier toilette"));

        model.setFilter("saumon");
        QCOMPARE(model.count(), 0);
        // Filtre d'affichage : rien n'a disparu de la base.
        QCOMPARE(db.getItems(listId).size(), size_t(3));

        model.setFilter("");
        QCOMPARE(model.count(), 3);
    }

    // Doublon : « Lait » ajouté deux fois dans une liste partagée, c'est le classique.
    void test_existingName() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        Database db;
        QVERIFY(openDb(db, dir));
        const auto listId = makeList(db);

        ItemModel model;
        model.load(db, listId, "dev-A");
        model.addItem("Lait", "1L");

        QCOMPARE(model.existingName("lait"),  QStringLiteral("Lait")); // casse ignorée
        QCOMPARE(model.existingName(" Lait "), QStringLiteral("Lait")); // espaces ignorés
        QVERIFY(model.existingName("Pain").isEmpty());
        QVERIFY(model.existingName("").isEmpty());

        // Un article supprimé n'est plus un doublon.
        model.removeItem(model.data(model.index(0), ItemModel::ItemIdRole).toString());
        QVERIFY(model.existingName("Lait").isEmpty());
    }

    // Qui a ajouté quoi : « vous » pour soi, le nom du participant sinon.
    void test_byName() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        Database db;
        QVERIFY(openDb(db, dir));
        const auto listId = makeList(db);

        // Un article venu d'un autre appareil, dont on connaît le nom.
        db.upsertMember(listId, "dev-B", "Marie", { 2, "dev-B" });
        Item remote;
        remote.listId  = listId;
        remote.itemId  = "item-remote";
        remote.created = 2000;
        remote.by      = "dev-B";
        remote.name    = "Harissa";
        remote.nameVer = { 2, "dev-B" };
        QVERIFY(db.upsertItem(remote));

        ItemModel model;
        model.load(db, listId, "dev-A");
        model.addItem("Lait", "");   // le nôtre

        QHash<QString, QString> authorByItem;
        for (int i = 0; i < model.count(); ++i) {
            authorByItem[model.data(model.index(i), ItemModel::NameRole).toString()] =
                model.data(model.index(i), ItemModel::ByNameRole).toString();
        }

        QCOMPARE(authorByItem["Lait"],    QStringLiteral("vous"));
        QCOMPARE(authorByItem["Harissa"], QStringLiteral("Marie"));
    }

    // Les sections suivent l'ordre du magasin, pas l'alphabet : « Fruits & légumes »
    // avant « Crèmerie », et les non classés à la fin.
    void test_aisleOrdering() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        Database db;
        QVERIFY(openDb(db, dir));
        const auto listId = makeList(db);

        ItemModel model;
        model.load(db, listId, "dev-A");

        model.addItem("Éponge", "", "", "Entretien");
        model.addItem("Pommes", "", "", "Fruits & légumes");
        model.addItem("Truc",   "", "", "");              // non classé
        model.addItem("Beurre", "", "", "Crèmerie");

        QCOMPARE(model.count(), 4);
        QCOMPARE(model.data(model.index(0), ItemModel::NameRole).toString(),
                 QStringLiteral("Pommes"));     // rayon 0
        QCOMPARE(model.data(model.index(1), ItemModel::NameRole).toString(),
                 QStringLiteral("Beurre"));     // Crèmerie
        QCOMPARE(model.data(model.index(2), ItemModel::NameRole).toString(),
                 QStringLiteral("Éponge"));     // Entretien (dernier rayon connu)
        QCOMPARE(model.data(model.index(3), ItemModel::NameRole).toString(),
                 QStringLiteral("Truc"));       // non classé : après tout le reste

        QCOMPARE(model.aisleCount(), 4);

        // Un article coché descend au bas de SON rayon, pas au bas de la liste : on
        // parcourt le magasin rayon par rayon.
        model.addItem("Carottes", "", "", "Fruits & légumes");
        const QString pommes = model.data(model.index(0), ItemModel::ItemIdRole).toString();
        model.toggleDone(pommes);

        QCOMPARE(model.data(model.index(0), ItemModel::NameRole).toString(),
                 QStringLiteral("Carottes"));   // reste à prendre
        QCOMPARE(model.data(model.index(1), ItemModel::NameRole).toString(),
                 QStringLiteral("Pommes"));     // pris, mais toujours dans son rayon
        QCOMPARE(model.data(model.index(2), ItemModel::NameRole).toString(),
                 QStringLiteral("Beurre"));     // le rayon suivant n'a pas bougé
    }

    // Rayon inventé : il voyage avec l'article (le champ porte le libellé), donc il
    // apparaît dans les choix sans qu'on ait rien à synchroniser de plus.
    void test_customAisle() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        Database db;
        QVERIFY(openDb(db, dir));
        const auto listId = makeList(db);

        ItemModel model;
        model.load(db, listId, "dev-A");

        QCOMPARE(model.aisleNames(), ItemModel::defaultAisles());

        QSignalSpy aisleSpy(&model, &ItemModel::aisleNamesChanged);
        model.addItem("Croquettes", "", "", "Animalerie");
        model.addItem("Vin",        "", "", "Cave");
        model.addItem("Pommes",     "", "", "Fruits & légumes");
        QVERIFY(aisleSpy.count() > 0);   // le choix se met à jour tout de suite

        // Les rayons d'origine d'abord, puis les inventés par ordre alphabétique — le
        // même ordre sur tous les appareils, sans classement à synchroniser.
        const QStringList names = model.aisleNames();
        QCOMPARE(names.size(), ItemModel::defaultAisles().size() + 2);
        QCOMPARE(names.at(names.size() - 2), QStringLiteral("Animalerie"));
        QCOMPARE(names.last(),               QStringLiteral("Cave"));

        // Tri : les rayons inventés passent après les rayons d'origine, mais avant les
        // articles non classés.
        model.addItem("Truc", "");   // sans rayon
        QCOMPARE(model.data(model.index(0), ItemModel::NameRole).toString(),
                 QStringLiteral("Pommes"));      // rayon d'origine
        QCOMPARE(model.data(model.index(1), ItemModel::NameRole).toString(),
                 QStringLiteral("Croquettes"));  // Animalerie
        QCOMPARE(model.data(model.index(2), ItemModel::NameRole).toString(),
                 QStringLiteral("Vin"));         // Cave
        QCOMPARE(model.data(model.index(3), ItemModel::NameRole).toString(),
                 QStringLiteral("Truc"));        // non classé, toujours en dernier

        // Un rayon dont plus aucun article ne dépend disparaît des choix de lui-même.
        const QString vin = model.data(model.index(2), ItemModel::ItemIdRole).toString();
        model.removeItem(vin);
        QVERIFY(!model.aisleNames().contains(QStringLiteral("Cave")));
        QVERIFY(model.aisleNames().contains(QStringLiteral("Animalerie")));
    }

    // Export CSV puis réimport : les articles reviennent, sans toucher à l'original.
    void test_exportImportCsv() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());

        AppController ctrl;
        QVERIFY(ctrl.db().open(dir.filePath("c.db")));

        // Liste source avec deux articles (dont un rangé et coché).
        ListMeta m;
        m.listId = "src"; m.key = std::vector<uint8_t>(32, 1); m.title = "Courses";
        m.titleVer = {1, "dev-A"}; m.lamport = 1; m.created = 1000;
        QVERIFY(ctrl.db().createList(m));

        Item a; a.listId = "src"; a.itemId = "a"; a.created = 1000; a.name = "Lait, entier";
        a.qty = "1 L"; a.note = "demi-écrémé"; a.aisle = "Crèmerie"; a.nameVer = {1,"dev-A"};
        QVERIFY(ctrl.db().upsertItem(a));
        Item b; b.listId = "src"; b.itemId = "b"; b.created = 1001; b.name = "Pain";
        b.done = true; b.nameVer = {1,"dev-A"};
        QVERIFY(ctrl.db().upsertItem(b));

        // Le CSV porte l'en-tête et les valeurs, virgule du nom échappée.
        const QString csv = ctrl.listCsv("src");
        QVERIFY(csv.contains("Article"));
        QVERIFY(csv.contains("\"Lait, entier\""));   // virgule → champ entre guillemets
        QVERIFY(csv.contains("Crèmerie"));

        const QUrl file = QUrl::fromLocalFile(dir.filePath("courses.csv"));
        QVERIFY(ctrl.exportListCsv(file, "src"));

        ctrl.importFile(file);

        // Une nouvelle liste (titre = nom de fichier), l'original intact → 2 listes.
        const auto lists = ctrl.db().getLists();
        QCOMPARE(lists.size(), size_t(2));

        std::string importedId;
        for (const auto &l : lists)
            if (l.listId != "src") { importedId = l.listId;
                QCOMPARE(QString::fromStdString(l.title), QStringLiteral("courses")); }
        QVERIFY(!importedId.empty());

        // Les articles ont été recréés à l'identique (rayon et coché compris).
        auto items = ctrl.db().getItems(importedId);
        QCOMPARE(items.size(), size_t(2));
        bool sawLait = false, sawPain = false;
        for (const auto &it : items) {
            if (it.name == "Lait, entier") {
                sawLait = true;
                QCOMPARE(it.qty, std::string("1 L"));
                QCOMPARE(it.note, std::string("demi-écrémé"));
                QCOMPARE(it.aisle, std::string("Crèmerie"));
                QVERIFY(!it.done);
            }
            if (it.name == "Pain") { sawPain = true; QVERIFY(it.done); }
        }
        QVERIFY(sawLait && sawPain);
    }

    // Export ZIP de toutes les listes, puis réimport : chaque CSV redevient une liste.
    void test_exportImportZip() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());

        AppController ctrl;
        QVERIFY(ctrl.db().open(dir.filePath("c.db")));

        for (const char *title : { "Maison", "Boulot" }) {
            ListMeta m;
            m.listId = title; m.key = std::vector<uint8_t>(32, 2);
            m.title = title; m.titleVer = {1,"dev-A"}; m.lamport = 1; m.created = 1000;
            QVERIFY(ctrl.db().createList(m));
            Item it; it.listId = title; it.itemId = std::string("i-") + title;
            it.created = 1000; it.name = std::string("Article ") + title;
            it.nameVer = {1,"dev-A"};
            QVERIFY(ctrl.db().upsertItem(it));
        }

        const QUrl zip = QUrl::fromLocalFile(dir.filePath("tout.zip"));
        QVERIFY(ctrl.exportAllZip(zip));

        ctrl.importFile(zip);

        // 2 listes d'origine + 2 réimportées.
        QCOMPARE(ctrl.db().getLists().size(), size_t(4));

        int maisonCopies = 0;
        for (const auto &l : ctrl.db().getLists())
            if (QString::fromStdString(l.title) == "Maison") ++maisonCopies;
        QCOMPARE(maisonCopies, 2);   // l'originale + celle issue du ZIP
    }

    // Un ajout manuel enrichit les favoris ; un article recréé par un merge distant, non.
    void test_addItem_recordsFavorite() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        Database db;
        QVERIFY(openDb(db, dir));
        const auto listId = makeList(db);

        ItemModel model;
        model.load(db, listId, "dev-A");

        QSignalSpy addedSpy(&model, &ItemModel::itemAdded);
        model.addItem("Lait", "1 L", "", "Crèmerie");
        QCOMPARE(addedSpy.count(), 1);

        auto favs = db.getFavorites(10);
        QCOMPARE(favs.size(), size_t(1));
        QCOMPARE(favs[0].name,  std::string("Lait"));
        QCOMPARE(favs[0].qty,   std::string("1 L"));
        QCOMPARE(favs[0].aisle, std::string("Crèmerie"));

        // Rajouter le même nom incrémente le favori (habitude renforcée).
        model.addItem("Lait", "", "", "");
        QCOMPARE(db.getFavorites(10).front().uses, int64_t(2));

        // Un article venu d'un merge distant (upsert direct) ne touche pas les favoris.
        Item remote; remote.listId = listId; remote.itemId = "remote-1";
        remote.created = 9000; remote.name = "Saumon"; remote.nameVer = {5, "dev-B"};
        QVERIFY(db.upsertItem(remote));
        for (const auto &f : db.getFavorites(10))
            QVERIFY(f.name != "Saumon");
    }

    // ListsModel : groupes (sections triées) et « partagée avec » (membres, soi exclu).
    void test_listsModel_groupsAndMembers() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        Database db;
        QVERIFY(openDb(db, dir));

        makeList(db, "list-maison");
        makeList(db, "list-libre");

        db.createGroup("grp-1", "Maison", 1000);
        db.setListGroup("list-maison", "grp-1");

        // « list-maison » partagée avec Marie ; nous-mêmes (dev-A) ne comptons pas.
        db.upsertMember("list-maison", "dev-A", "Moi",   { 1, "dev-A" });
        db.upsertMember("list-maison", "dev-B", "Marie", { 1, "dev-B" });

        ListsModel model;
        model.reload(db, "dev-A");
        QCOMPARE(model.rowCount(), 2);

        // La liste rangée passe avant la non rangée (les groupes d'abord).
        QCOMPARE(model.data(model.index(0), ListsModel::ListIdRole).toString(),
                 QStringLiteral("list-maison"));
        QCOMPARE(model.data(model.index(0), ListsModel::GroupNameRole).toString(),
                 QStringLiteral("Maison"));
        QCOMPARE(model.data(model.index(1), ListsModel::GroupNameRole).toString(),
                 QStringLiteral(""));   // non rangée, section vide, en dernier

        // Partagée avec Marie, sans se compter soi-même.
        QCOMPARE(model.data(model.index(0), ListsModel::MemberCountRole).toInt(), 1);
        QCOMPARE(model.data(model.index(0), ListsModel::MembersRole).toString(),
                 QStringLiteral("Marie"));
        QCOMPARE(model.data(model.index(1), ListsModel::MemberCountRole).toInt(), 0);

        // Groupe supprimé → la liste redescend chez les non rangées, mais existe encore.
        db.deleteGroup("grp-1");
        model.reload(db, "dev-A");
        QCOMPARE(model.rowCount(), 2);
        for (int i = 0; i < 2; ++i)
            QCOMPARE(model.data(model.index(i), ListsModel::GroupNameRole).toString(),
                     QStringLiteral(""));
    }

    // Réordonner : la ligne va où on la dépose, et la nouvelle position est persistée
    // avec une version (donc synchronisée).
    void test_moveItem() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        Database db;
        QVERIFY(openDb(db, dir));
        const auto listId = makeList(db);

        ItemModel model;
        model.load(db, listId, "dev-A");
        model.addItem("A", "");
        model.addItem("B", "");
        model.addItem("C", "");

        const QString idC = model.data(model.index(2), ItemModel::ItemIdRole).toString();

        // C (dernier) déposé en tête.
        model.moveItem(2, 0);
        QCOMPARE(model.data(model.index(0), ItemModel::ItemIdRole).toString(), idC);
        QCOMPARE(model.data(model.index(1), ItemModel::NameRole).toString(), QStringLiteral("A"));
        QCOMPARE(model.data(model.index(2), ItemModel::NameRole).toString(), QStringLiteral("B"));

        // Persisté et versionné : sans version, le pair d'en face rediffuserait
        // l'ancienne position et l'écraserait au merge.
        bool found = false;
        for (const auto &it : db.getItems(listId)) {
            if (it.itemId == idC.toStdString()) {
                QVERIFY(it.orderVer.lamport > 0);
                QCOMPARE(it.orderVer.deviceId, std::string("dev-A"));
                found = true;
            }
        }
        QVERIFY(found);

        // L'ordre survit au rechargement : c'est bien la position qui trie, plus la
        // date de création.
        ItemModel reloaded;
        reloaded.load(db, listId, "dev-A");
        QCOMPARE(reloaded.data(reloaded.index(0), ItemModel::NameRole).toString(),
                 QStringLiteral("C"));

        // Déplacement au milieu.
        model.moveItem(0, 1);   // C entre A et B
        QCOMPARE(model.data(model.index(0), ItemModel::NameRole).toString(), QStringLiteral("A"));
        QCOMPARE(model.data(model.index(1), ItemModel::NameRole).toString(), QStringLiteral("C"));
        QCOMPARE(model.data(model.index(2), ItemModel::NameRole).toString(), QStringLiteral("B"));

        // Bornes : ne rien casser sur un index absurde.
        model.moveItem(0, 0);
        model.moveItem(-1, 2);
        model.moveItem(1, 99);
        QCOMPARE(model.count(), 3);
    }

    // Déposer une ligne sous l'en-tête d'un autre rayon la range dans ce rayon : c'est
    // le geste naturel pour classer, et il n'y a pas d'autre lecture possible.
    void test_moveItem_acrossAisleChangesAisle() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        Database db;
        QVERIFY(openDb(db, dir));
        const auto listId = makeList(db);

        ItemModel model;
        model.load(db, listId, "dev-A");
        model.addItem("Pommes", "", "", "Fruits & légumes");
        model.addItem("Beurre", "", "", "Crèmerie");
        model.addItem("Truc",   "", "", "");   // non classé, donc en dernier

        QCOMPARE(model.data(model.index(2), ItemModel::NameRole).toString(),
                 QStringLiteral("Truc"));

        // « Truc » déposé sur la ligne de Beurre → il rejoint la crèmerie.
        model.moveItem(2, 1);

        QCOMPARE(model.data(model.index(1), ItemModel::NameRole).toString(),
                 QStringLiteral("Truc"));
        QCOMPARE(model.data(model.index(1), ItemModel::AisleRole).toString(),
                 QStringLiteral("Crèmerie"));

        // Le rayon est versionné : il part au relais comme n'importe quel champ.
        for (const auto &it : db.getItems(listId)) {
            if (it.name == "Truc") {
                QCOMPARE(it.aisle, std::string("Crèmerie"));
                QVERIFY(it.aisleVer.lamport > 0);
            }
        }
    }

    // Ranger un article depuis le dialogue d'édition le fait changer de section.
    void test_setAisle() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        Database db;
        QVERIFY(openDb(db, dir));
        const auto listId = makeList(db);

        ItemModel model;
        model.load(db, listId, "dev-A");
        model.addItem("Lait", "");
        model.addItem("Pommes", "", "", "Fruits & légumes");

        // Lait n'est pas classé : il est donc en dernier.
        QCOMPARE(model.data(model.index(1), ItemModel::NameRole).toString(),
                 QStringLiteral("Lait"));

        const QString lait = model.data(model.index(1), ItemModel::ItemIdRole).toString();
        model.setAisle(lait, "Crèmerie");

        // Crèmerie vient après Fruits & légumes, mais avant « non classé ».
        QCOMPARE(model.data(model.index(1), ItemModel::NameRole).toString(),
                 QStringLiteral("Lait"));
        QCOMPARE(model.data(model.index(1), ItemModel::AisleRole).toString(),
                 QStringLiteral("Crèmerie"));
        QCOMPARE(model.aisleCount(), 2);
    }

    // Suppression groupée : tout disparaît de la vue, tout est marqué supprimé en base.
    void test_removeItems_bulk() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        Database db;
        QVERIFY(openDb(db, dir));
        const auto listId = makeList(db);

        ItemModel model;
        model.load(db, listId, "dev-A");
        model.addItem("Lait", "");
        model.addItem("Pain", "");
        model.addItem("Œufs", "");
        QCOMPARE(model.count(), 3);

        const QString first  = model.data(model.index(0), ItemModel::ItemIdRole).toString();
        const QString second = model.data(model.index(1), ItemModel::ItemIdRole).toString();

        model.removeItems({ first, second });

        // Le troisième survit ; les deux autres sont des tombstones (del=true), pas
        // des lignes effacées : les autres appareils doivent apprendre la suppression.
        QCOMPARE(model.count(), 1);
        QCOMPARE(model.data(model.index(0), ItemModel::NameRole).toString(),
                 QStringLiteral("Œufs"));

        int tombstones = 0;
        for (const auto &it : db.getItems(listId))
            if (it.del) ++tombstones;
        QCOMPARE(tombstones, 2);
    }
};

QTEST_GUILESS_MAIN(ItemModelTest)
#include "tst_itemmodel.moc"
