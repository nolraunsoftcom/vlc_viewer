#pragma once

#include "VlcWidget.h"

#include <QDialog>
#include <QHash>
#include <QPointer>

class QLabel;
class QTreeWidget;
class QTreeWidgetItem;

class ChannelInfoDialog : public QDialog
{
    Q_OBJECT

public:
    explicit ChannelInfoDialog(VlcWidget *viewer, QWidget *parent = nullptr);

    void refresh();
    VlcWidget *viewer() const { return m_viewer.data(); }

private:
    QPointer<VlcWidget> m_viewer;
    QLabel *m_locationLabel = nullptr;
    QTreeWidget *m_statsTree = nullptr;
    QHash<QString, QTreeWidgetItem *> m_valueItems;

    QTreeWidgetItem *addGroup(const QString &title);
    void addMetric(QTreeWidgetItem *group, const QString &key, const QString &label);
    void setMetric(const QString &key, const QString &value);
};
