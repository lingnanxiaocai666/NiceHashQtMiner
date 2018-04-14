#include "Forms/Components/AlgorithmsListViewModel.h"


AlgorithmsListViewModel::AlgorithmsListViewModel(QObject* parent)
	: QAbstractTableModel(parent) { }

int AlgorithmsListViewModel::columnCount(const QModelIndex&/* parent*/) const
{
	return 5;
}

int AlgorithmsListViewModel::rowCount(const QModelIndex&/* parent*/) const
{
	return _data.count();
}

bool AlgorithmsListViewModel::setHeaderData(int section, Qt::Orientation orientation, const QVariant& value, int role)
{
	if (role!=Qt::EditRole || orientation!=Qt::Horizontal || section<0 || section>=5) {
		return false;
		}
	_header[section]=value.toString();
	return true;
}

QVariant AlgorithmsListViewModel::headerData(int section, Qt::Orientation orientation, int role) const
{
	if (role!=Qt::DisplayRole || orientation!=Qt::Horizontal) {
		return QVariant();
		}
	return _header.at(section);
}

bool AlgorithmsListViewModel::setData(const QModelIndex& index, const QVariant& value, int role)
{
	if (!index.isValid()) {
		return false;
		}
	int row=index.row(),
		col=index.column();
	switch (role) {
		case Qt::CheckStateRole:
			{
			Qt::CheckState state=value.value<Qt::CheckState>();
			_data[row].Checked=state;
			emit ItemChecked(row);
			}
			return true;
		case Qt::BackgroundColorRole:
			{
			QColor c=value.value<QColor>();
			_data[row].color=value.value<QColor>();
			return true;
			}
		case Qt::EditRole:
			if (col) {
				_data[row].SubItems[col]=value.toString();
				}
			else {
				Algorithm* algo=value.value<Algorithm*>();
				_data[row].Tag=algo;
				_data[row].Checked= algo->Enabled? Qt::Checked : Qt::Unchecked;
				}
			emit dataChanged(index, index);
			return true;
		}
	return false;
}

QVariant AlgorithmsListViewModel::data(const QModelIndex& index, int role) const
{
	if (!index.isValid()) {
		return QVariant();
		}

	int row=index.row();
	if (row>=_data.count() || row<0) {
		return QVariant();
		}

	int col=index.column();
	switch (role) {
		case Qt::DisplayRole: // 0
			{
			return col? _data.at(row).SubItems.at(col) : QVariant();
			}
//		case Qt::FontRole:
//		case Qt::BackgroundRole:
//		case Qt::TextAlignmentRole:
		case Qt::CheckStateRole: // 10
			if (col || !CheckBoxes) {
				return QVariant();
				}
			return _data.at(row).Checked;
		case Qt::EditRole: // 2
			return QVariant::fromValue<Algorithm*>(_data.at(row).Tag);
		case Qt::BackgroundColorRole: // 8
			return _data.at(row).color;
		}
	return QVariant();
}

Qt::ItemFlags AlgorithmsListViewModel::flags(const QModelIndex& index) const
{
	if (!index.isValid()) {
		return Qt::ItemIsEnabled;
		}
	return Qt::ItemIsUserCheckable | Qt::ItemNeverHasChildren | Qt::ItemIsEnabled | Qt::ItemIsSelectable;
//	return /*Qt::ItemIsEditable | */QAbstractTableModel::flags(index);
}

bool AlgorithmsListViewModel::insertRows(int row, int count, const QModelIndex&/* parent*/)
{
	beginInsertRows(QModelIndex(), row, row+count-1);

	for (int r=0; r<count; r++) {
		_data.insert(row, aItem());
		}

	endInsertRows();
	return true;
}

bool AlgorithmsListViewModel::removeRows(int row, int count, const QModelIndex&/* parent*/)
{
	beginRemoveRows(QModelIndex(), row, row+count-1);

	for (int r=0; r<count; r++) {
		_data.removeAt(row);
		}

	endRemoveRows();
	return true;
}