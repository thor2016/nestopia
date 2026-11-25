#include "debugger/BreakpointDialog.h"

#include <QCheckBox>
#include <QDialogButtonBox>
#include <QFontDatabase>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QRegularExpression>
#include <QVBoxLayout>

#include "NstApiDebugger.hpp"

bool BreakpointDialog::parseAddress(QString text, uint &address)
{
	text = text.trimmed();
	if (text.startsWith(QLatin1Char('$')))
	{
		text.remove(0, 1);
	}
	else if (text.startsWith(QStringLiteral("0x"), Qt::CaseInsensitive))
	{
		text.remove(0, 2);
	}
	const QRegularExpression pattern(QStringLiteral("^[0-9a-fA-F]{1,4}$"));
	if (!pattern.match(text).hasMatch())
	{
		return false;
	}
	bool ok = false;
	address = text.toUInt(&ok, 16);
	return ok;
}

BreakpointDialog::BreakpointDialog(const Values &initial, bool editing, const QSet<uint> &others, QWidget *parent)
	: QDialog(parent),
	  occupied(others),
	  addressEdit(new QLineEdit(this)),
	  conditionEdit(new QLineEdit(this)),
	  enabledBox(new QCheckBox(tr("Enabled"), this)),
	  problemLabel(new QLabel(this)),
	  buttons(new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this))
{
	setObjectName(QStringLiteral("debuggerBreakpointDialog"));
	setWindowTitle(editing ? tr("Edit Execute Breakpoint") : tr("Add Execute Breakpoint"));
	setModal(true);

	const QFont fixed = QFontDatabase::systemFont(QFontDatabase::FixedFont);
	addressEdit->setObjectName(QStringLiteral("breakpointDialogAddress"));
	addressEdit->setFont(fixed);
	addressEdit->setMaxLength(8);
	addressEdit->setPlaceholderText(tr("$8000"));
	addressEdit->setText(QLatin1Char('$') + QStringLiteral("%1").arg(initial.address, 4, 16, QLatin1Char('0')).toUpper());
	conditionEdit->setObjectName(QStringLiteral("breakpointDialogCondition"));
	conditionEdit->setFont(fixed);
	conditionEdit->setClearButtonEnabled(true);
	conditionEdit->setPlaceholderText(tr("Always break"));
	conditionEdit->setText(initial.condition);
	enabledBox->setObjectName(QStringLiteral("breakpointDialogEnabled"));
	enabledBox->setChecked(initial.enabled);
	problemLabel->setObjectName(QStringLiteral("breakpointDialogProblem"));
	problemLabel->setWordWrap(true);
	problemLabel->setStyleSheet(QStringLiteral("color: #c02020;"));
	buttons->setObjectName(QStringLiteral("breakpointDialogButtons"));

	auto *help = new QLabel(tr("The condition is evaluated before the instruction at the address runs. "
		"Registers: A X Y S P PC. Memory: [addr] byte, word[addr] 16 bit. Numbers: 10, $0A, 0x0A. "
		"Operators: + - * / % << >> & | ^ ~ ! == != < <= > >= && || and parentheses. "
		"Example: A == $10 && [$0010] != 0"), this);
	help->setWordWrap(true);
	help->setForegroundRole(QPalette::PlaceholderText);

	auto *form = new QFormLayout;
	form->addRow(tr("CPU address:"), addressEdit);
	form->addRow(tr("Type:"), new QLabel(tr("Execute"), this));
	form->addRow(tr("Condition:"), conditionEdit);
	form->addRow(QString(), enabledBox);
	auto *layout = new QVBoxLayout(this);
	layout->addLayout(form);
	layout->addWidget(problemLabel);
	layout->addWidget(help);
	layout->addWidget(buttons);
	setMinimumWidth(460);

	connect(addressEdit, &QLineEdit::textChanged, this, &BreakpointDialog::validate);
	connect(conditionEdit, &QLineEdit::textChanged, this, &BreakpointDialog::validate);
	connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
	connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
	validate();
	// Without a prefilled condition the next edit is most likely the condition
	(conditionEdit->text().isEmpty() ? conditionEdit : addressEdit)->setFocus();
}

BreakpointDialog::Values BreakpointDialog::values(void) const
{
	Values result;
	parseAddress(addressEdit->text(), result.address);
	result.condition = conditionEdit->text().trimmed();
	result.enabled = enabledBox->isChecked();
	return result;
}

void BreakpointDialog::validate(void)
{
	QString problem;
	uint address = 0;
	if (!parseAddress(addressEdit->text(), address))
	{
		problem = tr("Enter a hexadecimal CPU address from $0000 to $FFFF");
	}
	else if (occupied.contains(address))
	{
		problem = tr("A breakpoint already exists at that address");
	}
	else
	{
		std::string error;
		const QByteArray condition = conditionEdit->text().toUtf8();
		if (Nes::Api::Debugger::ValidateCondition(condition.constData(), &error) != Nes::RESULT_OK)
		{
			problem = tr("Condition: %1").arg(QString::fromStdString(error));
		}
	}
	problemLabel->setText(problem);
	buttons->button(QDialogButtonBox::Ok)->setEnabled(problem.isEmpty());
}
