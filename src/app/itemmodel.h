#pragma once

#include <QAbstractListModel>
#include <QString>
#include <QStringList>
#include <map>
#include <vector>
#include "../store/database.h"
#include "../core/types.h"

namespace app {

// ItemModel: QAbstractListModel for the items of a single list.
// Sorting (§7 SPEC): unchecked first, then by created ascending.
// Deleted items (del=true) are hidden (tombstone filter).
class ItemModel : public QAbstractListModel {
    Q_OBJECT
    Q_PROPERTY(int count READ count NOTIFY countChanged)
    // Articles déjà dans le panier : c'est la progression qu'affiche le mode Courses.
    Q_PROPERTY(int doneCount READ doneCount NOTIFY doneCountChanged)
    // Filtre de recherche (nom, quantité, description). Vide = tout est visible.
    // Filtre d'affichage seulement : rien n'est supprimé, rien n'est synchronisé.
    Q_PROPERTY(QString filter READ filter WRITE setFilter NOTIFY filterChanged)
    // Nombre de rayons distincts affichés. À un seul, les en-têtes de section ne
    // servent à rien : qui ne classe pas ses articles ne voit aucun changement.
    Q_PROPERTY(int aisleCount READ aisleCount NOTIFY countChanged)
    // Les rayons proposés à la saisie : ceux d'origine, puis ceux que les participants
    // ont inventés dans cette liste. NOTIFY et pas CONSTANT : un rayon créé doit
    // apparaître aussitôt dans le choix, ici comme chez les autres.
    Q_PROPERTY(QStringList aisleNames READ aisleNames NOTIFY aisleNamesChanged)

public:
    enum Roles {
        ItemIdRole = Qt::UserRole + 1,
        NameRole,
        QtyRole,
        NoteRole,
        DoneRole,
        DoneAtRole,
        CreatedRole,
        // Nom du participant qui a ajouté l'article ("vous" pour soi, vide si inconnu).
        ByNameRole,
        AisleRole,
    };

    // Rayons d'origine, dans l'ordre où on traverse un magasin. C'est cet ordre qui trie
    // les sections : la liste se lit comme un parcours, pas comme un alphabet.
    // Un rayon vide (« ») veut dire « non classé » et passe en dernier.
    static const QStringList &defaultAisles();

    explicit ItemModel(QObject *parent = nullptr);

    // Load items from DB for a given list.
    void load(store::Database &db, const std::string &listId, const std::string &deviceId);

    // ---- QAbstractListModel ----
    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;

    int count() const;
    int doneCount() const;
    int aisleCount() const;
    // Rayons d'origine + rayons inventés dans cette liste (déduits des articles :
    // aucun champ de plus à synchroniser, et un rayon dont plus rien ne dépend
    // disparaît de lui-même).
    QStringList aisleNames() const;

    QString filter() const { return m_filter; }
    void    setFilter(const QString &filter);

    // Nom d'un article déjà présent (comparaison insensible à la casse et aux
    // espaces), ou chaîne vide. Sert à prévenir avant d'ajouter un doublon.
    Q_INVOKABLE QString existingName(const QString &name) const;

public slots:
    void addItem(const QString &name, const QString &qty,
                 const QString &note = {}, const QString &aisle = {});

    // Déplacer une ligne visible : `from` et `to` sont des index de la vue. Franchir
    // une frontière de section change aussi le rayon de l'article — c'est le geste
    // naturel pour classer (« ce truc va à la crèmerie »), et il n'y a pas d'autre
    // interprétation raisonnable d'un dépôt sous un autre en-tête.
    void moveItem(int from, int to);

    // Ranger un article dans un rayon (chaîne vide = non classé).
    void setAisle(const QString &itemId, const QString &aisle);
    void toggleDone(const QString &itemId);
    void removeItem(const QString &itemId);
    // Suppression groupée (mode sélection) : un seul lot publié, pas N événements.
    void removeItems(const QStringList &itemIds);
    // Édition LWW du nom, de la quantité et de la description. Les champs inchangés
    // gardent leur version : réécrire une valeur identique ferait gagner ce champ
    // contre une modification distante concurrente qu'on n'a pas encore reçue.
    void editItem(const QString &itemId, const QString &name,
                  const QString &qty, const QString &note, const QString &aisle);

    // Fin de course : tout remettre à acheter (la liste se refait), ou retirer ce qui
    // a été pris (la liste se vide de ce qui est fait). Sans ces deux-là, un article
    // coché reste barré à l'écran pour toujours, et il faut le traiter un par un.
    void uncheckAll();
    void removeDone();

signals:
    void countChanged();
    void doneCountChanged();
    void filterChanged();
    void aisleNamesChanged();
    // Emitted after any local write (addItem, toggleDone, removeItem, editItem).
    void localChanged(const std::string& listId);

private:
    // Sorted visible rows (del=false only, sorted as §7).
    struct Row {
        core::Item item;
    };

    // Find visible row index by itemId (-1 if not found).
    int findRow(const QString &itemId) const;

    // Tri d'affichage : rayon (ordre du magasin), puis à acheter avant pris, puis
    // position manuelle. Un article coché descend au bas de SON rayon, pas au bas de
    // la liste : on parcourt le magasin rayon par rayon.
    static bool rowLessThan(const Row &a, const Row &b);

    // Rang du rayon dans le parcours. Rayon inconnu ou vide → après tous les autres.
    static int aisleRank(const std::string &aisle);

    // Full reload from m_items into m_rows (sorted, filtre appliqué).
    void rebuildRows();

    bool matchesFilter(const core::Item &item) const;

    // Ré-espace les positions d'un groupe (même rayon, même état) quand l'intervalle
    // entre deux voisins est épuisé.
    void renumber(const std::string &aisle, bool done);

    store::Database *m_db     = nullptr;
    std::string      m_listId;
    std::string      m_deviceId;
    QString          m_filter;

    // deviceId → nom affiché, pour dire qui a ajouté quoi. Chargé avec la liste.
    std::map<std::string, QString> m_memberNames;

    // Full set including tombstones (authoritative).
    std::vector<core::Item> m_items;

    // Visible, sorted view (no del=true).
    std::vector<Row> m_rows;
};

} // namespace app
