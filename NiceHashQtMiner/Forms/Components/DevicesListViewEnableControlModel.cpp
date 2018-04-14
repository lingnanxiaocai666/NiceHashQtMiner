#include "Forms/Components/DevicesListViewEnableControlModel.h"
#include "Devices/ComputeDevice/ComputeDevice.h"


DevicesListViewEnableControlModel::DevicesListViewEnableControlModel(QObject *parent)
	: QAbstractTableModel(parent) { }

int DevicesListViewEnableControlModel::rowCount(const QModelIndex&/* parent*/) const
{
	return _data.count();
}

int DevicesListViewEnableControlModel::columnCount(const QModelIndex&/* parent*/) const
{
	return 1;
}

QVariant DevicesListViewEnableControlModel::data(const QModelIndex& index, int role) const
{
	if (!index.isValid()) {
		return QVariant();
		}
	int row=index.row();
	if (row>=_data.count() || row<0) {
		return QVariant();
		}
	switch (role) {
		case Qt::DisplayRole: // 0
			return _data.at(row).Tag->GetFullName();
//		case Qt::DecorationRole: // 1
		case Qt::EditRole: // 2
			return QVariant::fromValue<ComputeDevice*>(_data.at(row).Tag);
//		case Qt::FontRole: // 6
//		case Qt::BackgroundRole:
//		case Qt::TextAlignmentRole: // 7
//		case Qt::ForegroundRole: // 9
		case Qt::CheckStateRole: // 10
			if (!CheckBoxes) {
				return QVariant();
				}
			return _data.at(row).Checked;
		case Qt::BackgroundColorRole: // 8
			return QVariant::fromValue<QColor>(_data.at(row).color);
		}
	return QVariant();
}

QVariant DevicesListViewEnableControlModel::headerData(int/* section*/, Qt::Orientation orientation, int role) const
{
	if (role!=Qt::DisplayRole || orientation!=Qt::Horizontal) {
		return QVariant();
		}
//	switch (section) {
//		case 0:
			return QString("Enabled");
//		case 1:
//			return QString("");
//		}
//	return QVariant();
}

bool DevicesListViewEnableControlModel::setData(const QModelIndex& index, const QVariant& value, int role)
{
	if (!index.isValid() || index.column()) {
		return false;
		}
	int row=index.row();
	switch (role) {
		case Qt::CheckStateRole:
			{
			Qt::CheckState state=value.value<Qt::CheckState>();
			_data[row].Checked=state;
			emit ItemChecked(row);
			}
			return true;
		case Qt::BackgroundColorRole:
			_data[row].color=value.value<QColor>();
			return true;
		case Qt::EditRole:
			ComputeDevice* cd=value.value<ComputeDevice*>();
			_data[row].Tag=cd;
			_data[row].Checked= cd->Enabled? Qt::Checked : Qt::Unchecked;
			emit dataChanged(index, index);
			return true;
		}
	return false;
}

Qt::ItemFlags DevicesListViewEnableControlModel::flags(const QModelIndex& index) const
{
	if (!index.isValid()) {
		return Qt::ItemIsEnabled;
		}
	return Qt::ItemIsUserCheckable | Qt::ItemNeverHasChildren | Qt::ItemIsEnabled | Qt::ItemIsSelectable;
//	return /*Qt::ItemIsEditable | */QAbstractTableModel::flags(index);
}

bool DevicesListViewEnableControlModel::insertRows(int row, int count, const QModelIndex&/* parent*/)
{
	beginInsertRows(QModelIndex(), row, row+count-1);

	for (int r=0; r<count; r++) {
		_data.insert(row, dItem());
		}

	endInsertRows();
	return true;
}

bool DevicesListViewEnableControlModel::removeRows(int row, int count, const QModelIndex&/* parent*/)
{
	beginRemoveRows(QModelIndex(), row, row+count-1);

	for (int r=0; r<count; r++) {
		_data.removeAt(row);
		}

	endRemoveRows();
	return true;
}