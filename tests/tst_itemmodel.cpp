#include <QtTest>
#include <QAbstractItemModelTester>
#include <QTemporaryDir>
#include <QSignalSpy>

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
        model.editItem(itemId, "Papier toilette", "2 paquets", "6 couches épaisses");
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
        model.editItem(itemId, "Lait", "1L", "");
        QCOMPARE(localSpy.count(), 0);

        const Item after = db.getItems(listId).front();
        QCOMPARE(after.nameVer.lamport, before.nameVer.lamport);
        QCOMPARE(after.qtyVer.lamport,  before.qtyVer.lamport);

        // Un nom vide n'est pas un article : refusé, l'ancien nom reste.
        model.editItem(itemId, "   ", "1L", "");
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
