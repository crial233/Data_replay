#ifndef SIMPLE_TABLE_MODEL_H
#define SIMPLE_TABLE_MODEL_H

#include <QAbstractTableModel>
#include <QStringList>
#include <QVector>

// 高性能表格模型：内部用 QVector<QStringList> 存储，一次性刷新视图
class SimpleTableModel : public QAbstractTableModel
{
    Q_OBJECT
public:
    explicit SimpleTableModel(const QStringList &headers, QObject *parent = nullptr)
        : QAbstractTableModel(parent), m_headers(headers) {}

    int rowCount(const QModelIndex &parent = QModelIndex()) const override
    {
        Q_UNUSED(parent);
        return m_rows.size();
    }
    int columnCount(const QModelIndex &parent = QModelIndex()) const override
    {
        Q_UNUSED(parent);
        return m_headers.size();
    }
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override
    {
        if (role != Qt::DisplayRole || !index.isValid()) return QVariant();
        if (index.row() >= m_rows.size()) return QVariant();
        const QStringList &row = m_rows[index.row()];
        if (index.column() >= row.size()) return QVariant();
        return row[index.column()];
    }
    QVariant headerData(int section, Qt::Orientation orientation, int role = Qt::DisplayRole) const override
    {
        if (role != Qt::DisplayRole) return QVariant();
        if (orientation == Qt::Horizontal && section < m_headers.size())
            return m_headers[section];
        return QVariant();
    }

    // 一次性替换所有数据，触发一次重绘
    void setRows(QVector<QStringList> rows)
    {
        beginResetModel();
        m_rows = std::move(rows);
        endResetModel();
    }

private:
    QStringList m_headers;
    QVector<QStringList> m_rows;
};

#endif // SIMPLE_TABLE_MODEL_H
