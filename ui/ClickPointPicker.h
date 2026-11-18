#pragma once
#include <QObject>
#include <QPoint>
#include <vector>

namespace mmr {

class PointMarker;

// The numbered, draggable circles the user drops on the spots the Auto Clicker should click.
// Points are physical virtual-desktop pixels (what SetCursorPos takes), exactly like recorded events.
// Hidden circles cost nothing: no window exists until show().
class ClickPointPicker : public QObject {
    Q_OBJECT
public:
    explicit ClickPointPicker(QObject* parent = nullptr);
    ~ClickPointPicker() override;

    void setPoints(const std::vector<QPoint>& nativePoints);  // saved positions (any number)
    const std::vector<QPoint>& points() const { return points_; }
    void setCount(int count);  // how many circles are in use; new ones get a free default spot
    int count() const { return count_; }

    void show();
    void hide();
    bool isShown() const { return shown_; }

signals:
    void pointsChanged();  // a circle was dropped on a new spot

private:
    void rebuild();
    void destroyMarkers();
    QPoint defaultPoint(int index, int total) const;

    std::vector<QPoint> points_;
    std::vector<PointMarker*> markers_;
    int count_ = 1;
    bool shown_ = false;
};

}  // namespace mmr
