#pragma once

#include <QDialog>
#include <QSet>
#include <QString>

class QCheckBox;
class QDialogButtonBox;
class QLabel;
class QLineEdit;

// Edits one execute breakpoint: CPU address, optional condition and enabled state.
class BreakpointDialog : public QDialog
{
	Q_OBJECT

	public:
		struct Values
		{
			uint address = 0;
			QString condition;
			bool enabled = true;
		};

		// occupied lists the addresses of other breakpoints, which cannot be used
		BreakpointDialog(const Values &initial, bool editing, const QSet<uint> &occupied, QWidget *parent = nullptr);
		Values values(void) const;

		// Accepts $hex, 0xhex and bare hex from 0 to FFFF
		static bool parseAddress(QString text, uint &address);

	private:
		void validate(void);

		QSet<uint> occupied;
		QLineEdit *addressEdit;
		QLineEdit *conditionEdit;
		QCheckBox *enabledBox;
		QLabel *problemLabel;
		QDialogButtonBox *buttons;
};
