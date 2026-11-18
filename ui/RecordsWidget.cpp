#include "RecordsWidget.h"

#include <QVBoxLayout>

#include "RecordItemWidget.h"
#include "ThemedDialog.h"

namespace mmr {

RecordsWidget::RecordsWidget(MacroRepository& repo, MacroController& controller, SettingsManager& settings,
                             ShortcutManager& shortcuts, QWidget* parent)
    : QWidget(parent), repo_(repo), c_(controller), settings_(settings), shortcuts_(shortcuts) {
    count_ = new QLabel;
    count_->setObjectName("sectionLabel");
    list_ = new QListWidget;
    list_->setObjectName("recordList");
    list_->setSpacing(6);
    list_->setSelectionMode(QAbstractItemView::SingleSelection);
    list_->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    list_->setFocusPolicy(Qt::NoFocus);
    empty_ = new QLabel;
    empty_->setObjectName("emptyState");
    empty_->setAlignment(Qt::AlignCenter);

    auto* lay = new QVBoxLayout(this);
    lay->setContentsMargins(18, 8, 18, 14);
    lay->addWidget(count_);
    lay->addWidget(list_, 1);
    lay->addWidget(empty_, 1);
    refreshEmptyText();
    connect(&shortcuts_, &ShortcutManager::bindingsChanged, this, &RecordsWidget::refreshEmptyText);

    connect(&c_, &MacroController::stateChanged, this, [this] { applyStates(); });
    connect(&c_, &MacroController::activeRecordChanged, this, [this] { applyStates(); });
    connect(&c_, &MacroController::recordSaved, this, [this](qint64 id) {
        refresh();
        for (int i = 0; i < list_->count(); ++i)
            if (list_->item(i)->data(Qt::UserRole).toLongLong() == id) list_->setCurrentRow(i);
    });
    refresh();
}

// The hint always shows the shortcut the user currently has, never a fixed key.
void RecordsWidget::refreshEmptyText() {
    const auto it = shortcuts_.active().find(ShortcutAction::StartRecording);
    const bool has = it != shortcuts_.active().end() && !it->second.isNone();
    const QString how = has ? QString("Press the record shortcut (%1)").arg(QString::fromStdString(toString(it->second)))
                            : QString("Press Start in the Recorder tab");
    empty_->setText("No records yet\n\n" + how + " and your\nactivity will show up here.");
}

qint64 RecordsWidget::selectedId() const {
    auto* it = list_->currentItem();
    return it ? it->data(Qt::UserRole).toLongLong() : -1;
}

void RecordsWidget::refresh() {
    const qint64 keep = selectedId();
    QString err;
    const auto records = repo_.list(&err);
    list_->clear();
    for (const auto& m : records) {
        auto* item = new QListWidgetItem;
        item->setData(Qt::UserRole, m.id);
        item->setSizeHint(QSize(0, 70));
        auto* w = new RecordItemWidget(m);
        list_->addItem(item);
        list_->setItemWidget(item, w);
        connect(w, &RecordItemWidget::playClicked, this, [this](qint64 id) { emit playRequested(id); });
        connect(w, &RecordItemWidget::pauseResumeClicked, &c_, &MacroController::pauseResumePlayback);
        connect(w, &RecordItemWidget::stopClicked, &c_, &MacroController::stopPlayback);
        connect(w, &RecordItemWidget::renameClicked, this, &RecordsWidget::rename);
        connect(w, &RecordItemWidget::deleteClicked, this, &RecordsWidget::remove);
        if (m.id == keep) list_->setCurrentItem(item);
    }
    const bool none = records.isEmpty();
    list_->setVisible(!none);
    empty_->setVisible(none);
    count_->setText(none ? "RECORDS" : QString("RECORDS  ·  %1").arg(records.size()));
    if (!err.isEmpty()) dialogs::message(this, "Records", "Could not read records: " + err, dialogs::Kind::Warning);
    applyStates();
}

void RecordsWidget::applyStates() {
    for (int i = 0; i < list_->count(); ++i)
        if (auto* w = qobject_cast<RecordItemWidget*>(list_->itemWidget(list_->item(i))))
            w->applyState(c_.state(), c_.activeRecordId());
}

void RecordsWidget::rename(qint64 id) {
    QString err;
    const auto m = repo_.meta(id, &err);
    if (!m) { dialogs::message(this, "Rename", err, dialogs::Kind::Warning); return; }
    bool ok = false;
    const QString name = dialogs::getText(this, "Rename record", "New name:", m->name, &ok);
    if (!ok) return;
    if (!repo_.rename(id, name, &err)) {
        dialogs::message(this, "Rename", err.isEmpty() ? "Name cannot be empty." : err, dialogs::Kind::Warning);
        return;
    }
    refresh();
}

void RecordsWidget::remove(qint64 id) {
    QString err;
    const auto m = repo_.meta(id, &err);
    if (!m) { dialogs::message(this, "Delete", err, dialogs::Kind::Warning); return; }
    if (settings_.general().confirmDelete) {
        if (!dialogs::confirm(this, "Delete record", QString("Delete \"%1\"? This cannot be undone.").arg(m->name))) return;
    }
    if (!repo_.remove(id, &err)) { dialogs::message(this, "Delete", err, dialogs::Kind::Warning); return; }
    refresh();
}

}  // namespace mmr
