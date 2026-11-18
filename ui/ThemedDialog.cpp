#include "ThemedDialog.h"

#include <QButtonGroup>
#include <QCheckBox>
#include <QDialog>
#include <QIntValidator>
#include <QGuiApplication>
#include <QScreen>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QLinearGradient>
#include <QMouseEvent>
#include <QPainter>
#include <QPushButton>
#include <QVBoxLayout>
#include <QWindow>

namespace mmr::dialogs {
namespace {
constexpr int kMargin = 12;  // transparent border around the card (same look as the main window)
constexpr int kRadius = 20;

class ThemedDialog : public QDialog {
public:
    ThemedDialog(QWidget* parent, const QString& title, const QString& text, const QColor& accent)
        : QDialog(parent, Qt::Dialog | Qt::FramelessWindowHint), accent_(accent) {
        setObjectName("themedDialog");
        setAttribute(Qt::WA_TranslucentBackground);
        setModal(true);
        setWindowTitle(title);

        auto* titleLabel = new QLabel(title.toUpper());
        titleLabel->setObjectName("appTitle");
        auto* body = new QLabel(text);
        body->setObjectName("dialogText");
        body->setTextFormat(Qt::PlainText);
        body->setWordWrap(true);

        buttons_ = new QHBoxLayout;
        buttons_->setSpacing(10);
        root_ = new QVBoxLayout(this);
        root_->setContentsMargins(kMargin + 20, kMargin + 16, kMargin + 20, kMargin + 18);
        root_->setSpacing(10);
        root_->addWidget(titleLabel);
        root_->addWidget(body, 1);
        root_->addSpacing(6);
        root_->addLayout(buttons_);
        setFixedWidth(400 + 2 * kMargin);
    }

    QLineEdit* addLineEdit(const QString& initial) {
        edit_ = new QLineEdit(initial);
        edit_->selectAll();
        root_->insertWidget(2, edit_);
        return edit_;
    }

    void addRow(QLayout* row) { root_->insertLayout(2, row); }  // above the buttons, like addLineEdit

    // Layout order: [cancel/No on the left] ...... [confirm/Yes on the right]
    QPushButton* addButton(const QString& text, bool confirmButton) {
        auto* b = new QPushButton(text);
        b->setObjectName(confirmButton ? "primaryButton" : "secondaryButton");
        b->setMinimumHeight(34);
        b->setMinimumWidth(96);
        b->setCursor(Qt::PointingHandCursor);
        b->setAutoDefault(false);
        connect(b, &QPushButton::clicked, this, confirmButton ? &QDialog::accept : &QDialog::reject);
        if (confirmButton) {
            if (buttons_->count() == 0) buttons_->addStretch(1);
            buttons_->addWidget(b);
        } else {
            buttons_->insertWidget(0, b);
            if (buttons_->count() == 1) buttons_->addStretch(1);
        }
        return b;
    }

    void run(QPushButton* defaultButton, QWidget* focus = nullptr) {
        defaultButton->setDefault(true);
        const int h = qMax(root_->totalHeightForWidth(width()), root_->totalMinimumSize().height());
        setFixedSize(width(), h);  // width is fixed; height follows the wrapped text
        centerOnOwner();
        (focus ? focus : edit_ ? static_cast<QWidget*>(edit_) : static_cast<QWidget*>(defaultButton))->setFocus();
        if (edit_) connect(edit_, &QLineEdit::returnPressed, this, &QDialog::accept);
        exec();
    }

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        const QRectF body = QRectF(rect()).adjusted(kMargin, kMargin, -kMargin, -kMargin);
        for (int i = 0; i < kMargin; ++i) {  // soft shadow
            p.setPen(Qt::NoPen);
            p.setBrush(QColor(0, 0, 0, 5 + i));
            p.drawRoundedRect(body.adjusted(-kMargin + i, -kMargin + i + 3, kMargin - i, kMargin - i + 3), kRadius + 6, kRadius + 6);
        }
        QLinearGradient bg(body.topLeft(), body.bottomRight());
        bg.setColorAt(0, QColor(30, 22, 27, 252));
        bg.setColorAt(0.55, QColor(15, 14, 18, 252));
        bg.setColorAt(1, QColor(22, 10, 14, 252));
        p.setBrush(bg);
        p.setPen(Qt::NoPen);
        p.drawRoundedRect(body, kRadius, kRadius);
        QColor edge = accent_;
        edge.setAlpha(150);
        p.setBrush(Qt::NoBrush);
        p.setPen(QPen(edge, 1.4));
        p.drawRoundedRect(body, kRadius, kRadius);
    }
    void mousePressEvent(QMouseEvent* e) override {  // drag the card by any empty spot
        if (e->button() == Qt::LeftButton) { if (auto* w = windowHandle()) w->startSystemMove(); e->accept(); }
        else QDialog::mousePressEvent(e);
    }

private:
    // Exact centre of the app window (or of the screen when there is none). Moving the dialog ourselves
    // stops Qt's own placement, which sits a little off-centre.
    void centerOnOwner() {
        QPoint c;
        if (parentWidget()) c = parentWidget()->window()->frameGeometry().center();
        else if (QScreen* sc = QGuiApplication::primaryScreen()) c = sc->availableGeometry().center();
        move(c.x() - width() / 2, c.y() - height() / 2);
    }

    QColor accent_;
    QVBoxLayout* root_;
    QHBoxLayout* buttons_;
    QLineEdit* edit_ = nullptr;
};

QColor accentFor(Kind k) {
    switch (k) {
        case Kind::Error: return QColor(255, 45, 70);
        case Kind::Warning: return QColor(255, 176, 46);
        default: return QColor(120, 124, 140);
    }
}
}  // namespace

void message(QWidget* parent, const QString& title, const QString& text, Kind kind) {
    ThemedDialog d(parent, title, text, accentFor(kind));
    d.run(d.addButton("OK", true));
}

bool confirm(QWidget* parent, const QString& title, const QString& text, const QString& yesText,
             const QString& noText, bool defaultYes) {
    ThemedDialog d(parent, title, text, accentFor(Kind::Warning));
    QPushButton* no = d.addButton(noText, false);   // left
    QPushButton* yes = d.addButton(yesText, true);  // right
    d.run(defaultYes ? yes : no);
    return d.result() == QDialog::Accepted;
}

QString getText(QWidget* parent, const QString& title, const QString& label, const QString& initial, bool* ok) {
    ThemedDialog d(parent, title, label, accentFor(Kind::Info));
    QLineEdit* edit = d.addLineEdit(initial);
    d.addButton("Cancel", false);
    QPushButton* okBtn = d.addButton("OK", true);
    d.run(okBtn);
    const bool accepted = d.result() == QDialog::Accepted;
    if (ok) *ok = accepted;
    return accepted ? edit->text() : QString();
}

PlaybackChoice choosePlaybackMode(QWidget* parent) {
    // Last choice is kept for the session so repeated runs do not need re-typing.
    static int lastUnit = 0;           // 0 = no pause, 1 = seconds, 2 = minutes, 3 = hours
    static QString lastValue[4];

    ThemedDialog d(parent, "Run record", "How should this record run? The first run starts right away.",
                   accentFor(Kind::Info));
    static const char* const kLabels[4] = {"Non-stop (no pause)", "Every N seconds", "Every N minutes", "Every N hours"};
    static const qint64 kUnitMs[4] = {0, 1000, 60'000, 3'600'000};

    auto* group = new QButtonGroup(&d);  // exclusive: ticking one unticks the others
    group->setExclusive(true);
    QCheckBox* checks[4];
    QLineEdit* fields[4] = {nullptr, nullptr, nullptr, nullptr};
    auto* rows = new QVBoxLayout;
    rows->setSpacing(4);
    for (int i = 0; i < 4; ++i) {
        checks[i] = new QCheckBox(kLabels[i]);
        checks[i]->setCursor(Qt::PointingHandCursor);
        group->addButton(checks[i], i);
        auto* row = new QHBoxLayout;
        row->addWidget(checks[i]);
        row->addStretch(1);
        if (i > 0) {
            fields[i] = new QLineEdit(lastValue[i]);
            fields[i]->setValidator(new QIntValidator(1, i == 1 ? 86400 : i == 2 ? 14400 : 720, fields[i]));  // <= 24 h / 10 d / 30 d
            fields[i]->setFixedWidth(90);
            fields[i]->setAlignment(Qt::AlignCenter);
            fields[i]->setPlaceholderText(i == 1 ? "seconds" : i == 2 ? "minutes" : "hours");
            fields[i]->setVisible(false);
            QSizePolicy sp = fields[i]->sizePolicy();
            sp.setRetainSizeWhenHidden(true);  // the dialog keeps one fixed height
            fields[i]->setSizePolicy(sp);
            row->addWidget(fields[i]);
        }
        rows->addLayout(row);
    }
    d.addRow(rows);

    d.addButton("Cancel", false);
    QPushButton* start = d.addButton("Start", true);

    auto refresh = [&] {
        const int u = group->checkedId();
        for (int i = 1; i < 4; ++i) fields[i]->setVisible(i == u);
        start->setEnabled(u <= 0 || (fields[u]->text().toInt() > 0));
        if (u > 0) fields[u]->setFocus();
    };
    for (int i = 1; i < 4; ++i) QObject::connect(fields[i], &QLineEdit::textChanged, &d, [&](const QString&) { refresh(); });
    QObject::connect(group, &QButtonGroup::idToggled, &d, [&](int, bool on) { if (on) refresh(); });
    checks[lastUnit]->setChecked(true);
    refresh();

    d.run(start, lastUnit > 0 ? static_cast<QWidget*>(fields[lastUnit]) : static_cast<QWidget*>(checks[0]));
    PlaybackChoice r;
    if (d.result() != QDialog::Accepted) return r;
    const int u = group->checkedId();
    lastUnit = u;
    if (u > 0) lastValue[u] = fields[u]->text();
    r.accepted = true;
    r.intervalMs = u > 0 ? qint64(fields[u]->text().toInt()) * kUnitMs[u] : 0;
    return r;
}

}  // namespace mmr::dialogs