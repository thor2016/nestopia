#include "input/InputConfigDialog.h"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QKeyEvent>
#include <QKeySequence>
#include <QLabel>
#include <QPushButton>
#include <QSignalBlocker>
#include <QTabWidget>
#include <QTableWidget>
#include <QVBoxLayout>

#include "core/EmulationThread.h"

namespace
{
	constexpr int kPollIntervalMs = 20;
	// Device list is checked every this many polls
	constexpr int kDeviceCheckTicks = 25;
}

InputConfigDialog::InputConfigDialog(EmulationThread &emulationThread, InputManager &inputManager, QWidget *parent)
	: QDialog(parent),
	  emulation(emulationThread),
	  input(inputManager),
	  tabs(nullptr),
	  hint(nullptr),
	  pollTicks(0)
{
	setWindowTitle(tr("Input Configuration"));

	{
		EmulationThread::Locker lock(emulation.mutex());
		edited = input.config();
	}

	tabs = new QTabWidget(this);

	for (int p = 0; p < InputConfig::kPlayers; ++p)
	{
		buildPage(p);
		tabs->addTab(pages[p].page, tr("Player %1").arg(p + 1));
	}

	hint = new QLabel(this);
	hint->setWordWrap(true);

	QDialogButtonBox *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);

	connect(buttons, &QDialogButtonBox::accepted, this, [this]() {
		for (int p = 0; p < InputConfig::kPlayers; ++p)
		{
			// Keep deriving the defaults from the device until the user touched them
			if (pages[p].touchedJoystick)
			{
				edited.players[p].joyConfigured = true;
			}
		}

		accept();
	});
	connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

	QVBoxLayout *layout = new QVBoxLayout(this);
	layout->addWidget(tabs);
	layout->addWidget(hint);
	layout->addWidget(buttons);

	populateDevices();

	for (int p = 0; p < InputConfig::kPlayers; ++p)
	{
		refreshTable(p);
	}

	updateHint();
	resize(520, 440);

	connect(&pollTimer, &QTimer::timeout, this, &InputConfigDialog::poll);
	pollTimer.start(kPollIntervalMs);
}

void InputConfigDialog::buildPage(int player)
{
	PlayerPage &page = pages[player];

	page.page = new QWidget(this);
	page.deviceCombo = new QComboBox(page.page);
	page.table = new QTableWidget(ButtonCount, 3, page.page);

	page.table->setHorizontalHeaderLabels({tr("Keyboard"), tr("Joystick"), tr("Joystick (alt)")});
	page.table->setVerticalHeaderLabels([] {
		QStringList names;

		for (int b = 0; b < ButtonCount; ++b)
		{
			names << QString::fromLatin1(nesButtonName(b));
		}

		return names;
	}());
	page.table->setEditTriggers(QAbstractItemView::NoEditTriggers);
	page.table->setSelectionMode(QAbstractItemView::SingleSelection);
	page.table->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);

	for (int b = 0; b < ButtonCount; ++b)
	{
		for (int c = 0; c < 3; ++c)
		{
			page.table->setItem(b, c, new QTableWidgetItem());
		}
	}

	QPushButton *defaults = new QPushButton(tr("Joystick Defaults"), page.page);
	QPushButton *clear = new QPushButton(tr("Clear All"), page.page);

	QHBoxLayout *deviceRow = new QHBoxLayout();
	deviceRow->addWidget(new QLabel(tr("Joystick:"), page.page));
	deviceRow->addWidget(page.deviceCombo, 1);

	QHBoxLayout *buttonRow = new QHBoxLayout();
	buttonRow->addWidget(defaults);
	buttonRow->addWidget(clear);
	buttonRow->addStretch(1);

	QVBoxLayout *layout = new QVBoxLayout(page.page);
	layout->addLayout(deviceRow);
	layout->addWidget(page.table, 1);
	layout->addLayout(buttonRow);

	connect(page.table, &QTableWidget::cellClicked, this, &InputConfigDialog::onCellClicked);
	connect(page.deviceCombo, &QComboBox::currentIndexChanged, this, &InputConfigDialog::onDeviceChanged);
	connect(defaults, &QPushButton::clicked, this, &InputConfigDialog::onUseJoystickDefaults);
	connect(clear, &QPushButton::clicked, this, &InputConfigDialog::onClearPlayer);
}

int InputConfigDialog::currentPlayer(void) const
{
	return tabs->currentIndex();
}

int InputConfigDialog::resolveDevice(int player) const
{
	// Mirrors InputManager::resolveJoystick for the edited selection
	const QString &selection = edited.players[player].joyDevice;
	const JoystickSystem &joysticks = input.joysticks();

	if (selection == QLatin1String("none"))
	{
		return -1;
	}

	if (selection.isEmpty())
	{
		return player < joysticks.deviceCount() ? player : -1;
	}

	return joysticks.findDevice(selection);
}

void InputConfigDialog::populateDevices(void)
{
	QStringList keys;
	QStringList names;

	{
		EmulationThread::Locker lock(emulation.mutex());
		input.joysticks().update();

		for (int i = 0; i < input.joysticks().deviceCount(); ++i)
		{
			keys << input.joysticks().device(i).key;
			names << input.joysticks().device(i).name;
		}
	}

	deviceKeys = keys;

	for (int p = 0; p < InputConfig::kPlayers; ++p)
	{
		QComboBox *combo = pages[p].deviceCombo;
		const QSignalBlocker blocker(combo);
		const QString selection = edited.players[p].joyDevice;

		combo->clear();
		combo->addItem(tr("Automatic"), QString());
		combo->addItem(tr("None"), QStringLiteral("none"));

		for (int i = 0; i < keys.size(); ++i)
		{
			combo->addItem(names[i], keys[i]);
		}

		int index = combo->findData(selection);

		if (index < 0)
		{
			// Keep the setting of a device that is not connected right now
			combo->addItem(tr("%1 (not connected)").arg(selection), selection);
			index = combo->count() - 1;
		}

		combo->setCurrentIndex(index);
	}
}

QString InputConfigDialog::cellText(int player, int button, int column) const
{
	const PlayerBindings &bindings = edited.players[player];

	if (capture.player == player && capture.button == button && capture.column == column)
	{
		return tr("Press an input...");
	}

	if (column == 0)
	{
		return bindings.keys[button] != 0 ? QKeySequence(bindings.keys[button]).toString() : QString();
	}

	return bindings.joy[button][column - 1].displayName();
}

void InputConfigDialog::refreshTable(int player)
{
	for (int b = 0; b < ButtonCount; ++b)
	{
		for (int c = 0; c < 3; ++c)
		{
			pages[player].table->item(b, c)->setText(cellText(player, b, c));
		}
	}
}

void InputConfigDialog::updateHint(void)
{
	if (capture.player < 0)
	{
		hint->setText(tr("Click a cell, then press a key or move a joystick input. Esc cancels, Delete clears the cell."));
	}
	else if (capture.column > 0 && !capture.baselineValid)
	{
		hint->setText(tr("No joystick is connected for this player."));
	}
	else
	{
		hint->setText(tr("Waiting for input. Esc cancels, Delete clears the cell."));
	}
}

void InputConfigDialog::onCellClicked(int row, int column)
{
	startCapture(currentPlayer(), row, column);
}

void InputConfigDialog::startCapture(int player, int row, int column)
{
	endCapture();

	capture.player = player;
	capture.button = row;
	capture.column = column;
	capture.baselineValid = false;

	if (column > 0)
	{
		EmulationThread::Locker lock(emulation.mutex());

		input.joysticks().update();

		const int device = resolveDevice(player);

		if (device >= 0 && device < input.joysticks().deviceCount())
		{
			capture.baseline = input.joysticks().device(device).state;
			capture.baselineValid = true;
		}
	}

	refreshTable(player);
	updateHint();
	setFocus();
}

void InputConfigDialog::endCapture(void)
{
	const int player = capture.player;

	capture = Capture();

	if (player >= 0)
	{
		refreshTable(player);
	}

	updateHint();
}

void InputConfigDialog::assignKey(int key)
{
	edited.players[capture.player].keys[capture.button] = key;
	endCapture();
}

void InputConfigDialog::assignJoystick(const JoyBinding &binding)
{
	edited.players[capture.player].joy[capture.button][capture.column - 1] = binding;
	pages[capture.player].touchedJoystick = true;
	endCapture();
}

void InputConfigDialog::clearCell(void)
{
	if (capture.column == 0)
	{
		assignKey(0);
	}
	else
	{
		assignJoystick(JoyBinding());
	}
}

void InputConfigDialog::keyPressEvent(QKeyEvent *event)
{
	if (capture.player < 0)
	{
		QDialog::keyPressEvent(event);
		return;
	}

	switch (event->key())
	{
		case Qt::Key_Escape:
			endCapture();
			break;
		case Qt::Key_Delete:
		case Qt::Key_Backspace:
			clearCell();
			break;
		default:
			if (capture.column == 0 && event->key() != Qt::Key_unknown)
			{
				assignKey(event->key());
			}
			break;
	}
}

void InputConfigDialog::onDeviceChanged(void)
{
	for (int p = 0; p < InputConfig::kPlayers; ++p)
	{
		edited.players[p].joyDevice = pages[p].deviceCombo->currentData().toString();
	}

	endCapture();
}

void InputConfigDialog::onUseJoystickDefaults(void)
{
	const int player = currentPlayer();

	{
		EmulationThread::Locker lock(emulation.mutex());
		input.joysticks().defaultBindings(resolveDevice(player), edited.players[player].joy);
	}

	pages[player].touchedJoystick = true;
	endCapture();
	refreshTable(player);
}

void InputConfigDialog::onClearPlayer(void)
{
	const int player = currentPlayer();
	PlayerBindings &bindings = edited.players[player];

	for (int b = 0; b < ButtonCount; ++b)
	{
		bindings.keys[b] = 0;
		bindings.joy[b][0] = bindings.joy[b][1] = JoyBinding();
	}

	pages[player].touchedJoystick = true;
	endCapture();
	refreshTable(player);
}

void InputConfigDialog::poll(void)
{
	const bool capturing = capture.player >= 0 && capture.column > 0 && capture.baselineValid;
	const bool checkDevices = ++pollTicks % kDeviceCheckTicks == 0;

	if (!capturing && !checkDevices)
	{
		return;
	}

	JoyState state;
	bool haveState = false;
	QStringList keys;

	{
		EmulationThread::Locker lock(emulation.mutex());
		JoystickSystem &joysticks = input.joysticks();

		joysticks.update();

		for (int i = 0; i < joysticks.deviceCount(); ++i)
		{
			keys << joysticks.device(i).key;
		}

		if (capturing)
		{
			const int device = resolveDevice(capture.player);

			if (device >= 0 && device < joysticks.deviceCount())
			{
				state = joysticks.device(device).state;
				haveState = true;
			}
		}
	}

	if (keys != deviceKeys)
	{
		const Capture previous = capture;

		populateDevices();

		// Devices came or went, the baseline refers to the old list
		if (previous.player >= 0)
		{
			endCapture();
		}

		return;
	}

	JoyBinding found;

	if (haveState && state.detectNew(capture.baseline, found))
	{
		assignJoystick(found);
	}
}
