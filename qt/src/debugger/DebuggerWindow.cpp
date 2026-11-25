#include "debugger/DebuggerWindow.h"

#include <memory>

#include <QAction>
#include <QActionGroup>
#include <QCheckBox>
#include <QFontDatabase>
#include <QFontDialog>
#include <QFormLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QHeaderView>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QMenuBar>
#include <QPainter>
#include <QPainterPath>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QRegularExpressionValidator>
#include <QSettings>
#include <QSet>
#include <QShowEvent>
#include <QSignalBlocker>
#include <QSplitter>
#include <QTableWidget>
#include <QTimer>
#include <QToolBar>
#include <QVBoxLayout>

#include "debugger/BreakpointDialog.h"
#include "debugger/DebuggerTabs.h"
#include "debugger/DisassemblyView.h"

namespace
{
	QString hex(qulonglong value, int width = 2)
	{
		return QStringLiteral("%1").arg(value, width, 16, QLatin1Char('0')).toUpper();
	}

	QString addressText(uint address)
	{
		return QLatin1Char('$') + hex(address, 4);
	}

	enum class Glyph { GoTo, Run, Pause, Into, Out, Over, Cursor, Scanline, Frame, Reset };

	QIcon debugIcon(Glyph glyph)
	{
		QIcon icon;
		for (int scale : {1, 2})
		{
			QPixmap pixmap(24 * scale, 24 * scale);
			pixmap.fill(Qt::transparent);
			QPainter painter(&pixmap);
			painter.scale(scale, scale);
			painter.setRenderHint(QPainter::Antialiasing);
			painter.setPen(QPen(QColor(40, 100, 175), 2, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
			painter.setBrush(Qt::NoBrush);
			const auto arrow = [&painter](QPoint tip, bool down) {
				const int delta = down ? -4 : 4;
				painter.drawLine(tip, tip + QPoint(-4, delta));
				painter.drawLine(tip, tip + QPoint(4, delta));
			};
			switch (glyph)
			{
				case Glyph::Run:
					painter.setPen(QColor(40, 115, 45));
					painter.setBrush(QColor(65, 165, 70));
					painter.drawPolygon(QPolygon({QPoint(6, 3), QPoint(21, 12), QPoint(6, 21)}));
					break;
				case Glyph::Pause:
					painter.setPen(Qt::NoPen);
					painter.setBrush(QColor(210, 135, 35));
					painter.drawRoundedRect(5, 3, 5, 18, 1, 1);
					painter.drawRoundedRect(14, 3, 5, 18, 1, 1);
					break;
				case Glyph::Into:
				case Glyph::Out:
					painter.drawLine(4, 20, 20, 20);
					painter.drawLine(12, 3, 12, 15);
					arrow(glyph == Glyph::Into ? QPoint(12, 15) : QPoint(12, 3), glyph == Glyph::Into);
					break;
				case Glyph::Over:
				{
					painter.drawLine(4, 20, 20, 20);
					QPainterPath path;
					path.moveTo(3, 13);
					path.cubicTo(3, 1, 20, 1, 20, 13);
					painter.drawPath(path);
					arrow(QPoint(20, 13), true);
					break;
				}
				case Glyph::Scanline:
				case Glyph::Frame:
					painter.drawRoundedRect(2, 3, 20, 17, 2, 2);
					for (int y = 7; y < 19; y += 4)
					{
						painter.drawLine(5, y, glyph == Glyph::Scanline && y != 11 ? 12 : 19, y);
					}
					painter.setPen(QPen(QColor(55, 155, 70), 2));
					painter.drawLine(15, 11, 20, 11);
					break;
				case Glyph::Cursor:
					painter.setBrush(QColor(65, 165, 70));
					painter.drawPolygon(QPolygon({QPoint(3, 5), QPoint(12, 12), QPoint(3, 19)}));
					painter.drawLine(17, 3, 17, 21);
					break;
				case Glyph::GoTo:
					painter.drawEllipse(6, 6, 12, 12);
					painter.drawLine(12, 1, 12, 9);
					painter.drawLine(12, 15, 12, 23);
					painter.drawLine(1, 12, 9, 12);
					painter.drawLine(15, 12, 23, 12);
					break;
				case Glyph::Reset:
					painter.drawArc(QRect(4, 4, 16, 16), 40 * 16, 285 * 16);
					painter.drawLine(20, 3, 20, 10);
					painter.drawLine(14, 8, 20, 10);
					break;
			}
			painter.end();
			icon.addPixmap(pixmap);
		}
		return icon;
	}

	QLineEdit *dataField(QWidget *parent, const QString &name, int width, bool readOnly = true)
	{
		auto *field = new QLineEdit(parent);
		field->setObjectName(name);
		field->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
		field->setAlignment(Qt::AlignCenter);
		field->setReadOnly(readOnly);
		field->setMinimumWidth(field->fontMetrics().horizontalAdvance(QLatin1Char('0')) * width + 18);
		field->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
		return field;
	}

	QTableWidgetItem *tableItem(const QString &text)
	{
		auto *item = new QTableWidgetItem(text);
		item->setFlags(item->flags() & ~Qt::ItemIsEditable);
		return item;
	}
}

DebuggerWindow::DebuggerWindow(EmulationThread &thread, QWidget *parent)
	: QDialog(parent, Qt::Window),
	  emulation(thread),
	  refreshTimer(new QTimer(this)),
	  toolbar(new QToolBar(this)),
	  horizontal(new QSplitter(Qt::Horizontal, this)),
	  disassembly(new DisassemblyView(this)),
	  breakpointTable(new QTableWidget(this)),
	  bookmarkList(new QListWidget(this)),
	  stackText(new QPlainTextEdit(this)),
	  statusLabel(new QLabel(this)),
	  messageLabel(new QLabel(this))
{
	setObjectName(QStringLiteral("debuggerWindow"));
	setWindowTitle(tr("6502 Debugger - NestopiaQt"));
	resize(1120, 760);
	auto *layout = new QVBoxLayout(this);
	layout->setContentsMargins(6, 6, 6, 6);
	layout->setSpacing(4);
	auto *menuBar = new QMenuBar(this);
	menuBar->setNativeMenuBar(false);
	layout->setMenuBar(menuBar);
	toolbar->setObjectName(QStringLiteral("debuggerToolbar"));
	toolbar->setMovable(false);
	toolbar->setToolButtonStyle(Qt::ToolButtonIconOnly);
	toolbar->setIconSize(QSize(24, 24));
	buildMenus(menuBar, toolbar);
	layout->addWidget(toolbar);
	layout->addWidget(horizontal, 1);
	horizontal->addWidget(disassembly);
	for (int column = 0; column < 2; ++column)
	{
		columns[column] = new QSplitter(Qt::Vertical, horizontal);
		columns[column]->setChildrenCollapsible(false);
		for (int row = 0; row < 4; ++row)
		{
			auto *group = new DebuggerTabs(columns[column]);
			group->setObjectName(QStringLiteral("debuggerTabs%1%2").arg(column).arg(row));
			tabs[column][row] = group;
			connect(group, &DebuggerTabs::moveRequested, this, &DebuggerWindow::movePanel);
			connect(group, &DebuggerTabs::arrangementChanged, this, &DebuggerWindow::updateTabVisibility);
		}
	}
	buildPanels();
	restoreLayout();
	statusLabel->setObjectName(QStringLiteral("debuggerStatus"));
	messageLabel->setObjectName(QStringLiteral("debuggerMessage"));
	messageLabel->setWordWrap(true);
	layout->addWidget(statusLabel);
	layout->addWidget(messageLabel);
	connect(disassembly, &DisassemblyView::breakpointRequested, this, [this](uint address) {
		setBreakpoint(address, !breakpoints.value(address, false));
	});
	connect(disassembly, &DisassemblyView::breakpointDialogRequested, this, [this](uint address) {
		openBreakpointDialog(address, breakpoints.contains(address));
	});
	connect(disassembly, &DisassemblyView::bookmarkRequested, this, &DebuggerWindow::addBookmarkAt);
	connect(disassembly, &DisassemblyView::runToRequested, this, &DebuggerWindow::runToAddress);

	refreshTimer->setInterval(200);
	connect(refreshTimer, &QTimer::timeout, this, &DebuggerWindow::refresh);
	connect(&emulation, &EmulationThread::halted, this, [this](quint16, int) {
		snapshotDirty = true;
		followPC = true;
		refresh();
	}, Qt::QueuedConnection);
	connect(&emulation, &EmulationThread::resumed, this, &DebuggerWindow::refresh, Qt::QueuedConnection);
	connect(this, &QDialog::finished, this, [this]() {
		refreshTimer->stop();
		saveLayout();
		if (emulation.isDebuggerEnabled())
		{
			emulation.setDebuggerEnabled(false);
		}
		emit closed();
	});
}

DebuggerWindow::~DebuggerWindow(void)
{
	if (emulation.isDebuggerEnabled())
	{
		emulation.setDebuggerEnabled(false);
	}
}

void DebuggerWindow::buildMenus(QMenuBar *menuBar, QToolBar *bar)
{
	auto *fileMenu = menuBar->addMenu(tr("&File"));
	auto *closeAction = fileMenu->addAction(tr("&Close"), this, &QDialog::close);
	closeAction->setShortcut(QKeySequence::Close);
	auto *viewMenu = menuBar->addMenu(tr("&View"));
	auto *debugMenu = menuBar->addMenu(tr("&Debug"));
	auto *optionsMenu = menuBar->addMenu(tr("&Options"));
	auto *goTo = viewMenu->addAction(debugIcon(Glyph::GoTo), tr("Go to &Address..."), this,
		&DebuggerWindow::goToAddress);
	goTo->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_A));
	goTo->setObjectName(QStringLiteral("debuggerGoToAction"));
	bar->addAction(goTo);
	auto *pcAction = viewMenu->addAction(tr("Go to &PC"), disassembly, &DisassemblyView::goToPC);
	pcAction->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_G));
	auto *back = viewMenu->addAction(tr("Navigate &Back"), disassembly, &DisassemblyView::navigateBack);
	back->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_Left));
	auto *forward = viewMenu->addAction(tr("Navigate &Forward"), disassembly, &DisassemblyView::navigateForward);
	forward->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_Right));
	viewMenu->addSeparator();
	auto *presets = viewMenu->addMenu(tr("&Layout Presets"));
	auto *group = new QActionGroup(this);
	const QString names[] = {tr("Compact"), tr("Compact Split"), tr("Wide"), tr("Wide Quad")};
	for (int index = 0; index < 4; ++index)
	{
		auto *action = presets->addAction(names[index]);
		action->setCheckable(true);
		action->setObjectName(QStringLiteral("debuggerLayout%1").arg(index + 1));
		group->addAction(action);
		presetActions[index] = action;
		connect(action, &QAction::triggered, this, [this, index]() { setLayoutPreset(index + 1); });
	}
	auto *fontAction = viewMenu->addAction(tr("Assembly &Font..."), this, [this]() {
		bool ok = false;
		const QFont font = QFontDialog::getFont(&ok, disassembly->font(), this, tr("Assembly Font"),
			QFontDialog::MonospacedFonts);
		if (ok)
		{
			disassembly->setFont(font);
		}
	});
	fontAction->setObjectName(QStringLiteral("debuggerFontAction"));
	auto *placement = viewMenu->addMenu(tr("PC Line Positioning"));
	auto *placementGroup = new QActionGroup(this);
	QSettings settings;
	const bool pcCentered = settings.value(QStringLiteral("debugger/pcCentered"), true).toBool();
	disassembly->setPCPlacement(pcCentered);
	for (bool center : {false, true})
	{
		auto *action = placement->addAction(center ? tr("Center") : tr("Top Line"));
		action->setObjectName(center ? QStringLiteral("debuggerPCCentered") : QStringLiteral("debuggerPCAtTop"));
		action->setCheckable(true);
		action->setChecked(center == pcCentered);
		placementGroup->addAction(action);
		connect(action, &QAction::triggered, this, [this, center]() { disassembly->setPCPlacement(center); });
	}
	viewMenu->addAction(tr("&Refresh Snapshot"), this, [this]() { snapshotDirty = true; refresh(); });
	bar->addSeparator();
	const auto command = [this, debugMenu, bar](const QString &text, const char *name, Glyph glyph,
		QKeySequence shortcut, auto handler) {
		auto *action = debugMenu->addAction(debugIcon(glyph), text);
		action->setObjectName(QLatin1String(name));
		action->setShortcut(shortcut);
		action->setToolTip(shortcut.isEmpty() ? text : tr("%1 (%2)").arg(text, shortcut.toString()));
		connect(action, &QAction::triggered, this, handler);
		bar->addAction(action);
		return action;
	};
	haltActions.append(command(tr("Run"), "debuggerRunAction", Glyph::Run, QKeySequence(Qt::Key_F5),
		[this]() { resume(Request::RUN); }));
	breakAction = command(tr("Break"), "debuggerBreakAction", Glyph::Pause, QKeySequence(Qt::Key_F6), [this]() {
		emulation.requestBreak();
		setStatus(tr("Break requested"));
	});
	debugMenu->addSeparator();
	haltActions.append(command(tr("Step Into"), "debuggerStepAction", Glyph::Into, QKeySequence(Qt::Key_F11),
		[this]() { resume(Request::STEP_INTO); }));
	haltActions.append(command(tr("Step Out"), "debuggerStepOutAction", Glyph::Out,
		QKeySequence(Qt::SHIFT | Qt::Key_F11), [this]() { resume(Request::STEP_OUT); }));
	haltActions.append(command(tr("Step Over"), "debuggerStepOverAction", Glyph::Over, QKeySequence(Qt::Key_F10),
		[this]() { resume(Request::STEP_OVER); }));
	bar->addSeparator();
	debugMenu->addSeparator();
	haltActions.append(command(tr("Run to Cursor"), "debuggerRunToAction", Glyph::Cursor,
		QKeySequence(Qt::CTRL | Qt::Key_F10), [this]() { runToAddress(disassembly->selectedAddress()); }));
	haltActions.append(command(tr("Run Line"), "debuggerScanlineAction", Glyph::Scanline, QKeySequence(Qt::Key_F7),
		[this]() { resume(Request::RUN_TO_SCANLINE); }));
	haltActions.append(command(tr("Run Frame"), "debuggerFrameAction", Glyph::Frame, QKeySequence(Qt::Key_F8),
		[this]() { resume(Request::RUN_TO_FRAME); }));
	bar->addSeparator();
	auto *resetCounters = command(tr("Reset Instruction Counter"), "debuggerResetCounter", Glyph::Reset, {}, [this]() {
		EmulationThread::Locker lock(emulation.mutex());
		emulation.core().debugger().ResetInstructionCount();
	});
	resetCounters->setToolTip(tr("Reset the debugger instruction count; the emulation clock is unchanged"));
	auto *bytes = optionsMenu->addAction(tr("Show Byte Codes"));
	bytes->setObjectName(QStringLiteral("debuggerShowBytes"));
	bytes->setCheckable(true);
	bytes->setChecked(settings.value(QStringLiteral("debugger/showBytes"), true).toBool());
	disassembly->setShowBytes(bytes->isChecked());
	connect(bytes, &QAction::toggled, disassembly, &DisassemblyView::setShowBytes);
	auto *rom = optionsMenu->addAction(tr("Display ROM Offsets"));
	rom->setObjectName(QStringLiteral("debuggerShowRomOffsets"));
	rom->setCheckable(true);
	rom->setChecked(settings.value(QStringLiteral("debugger/showRomOffsets"), false).toBool());
	disassembly->setShowRomOffsets(rom->isChecked());
	connect(rom, &QAction::toggled, disassembly, &DisassemblyView::setShowRomOffsets);
}

void DebuggerWindow::buildPanels(void)
{
	const QFont fixed = QFontDatabase::systemFont(QFontDatabase::FixedFont);
	for (int index = 0; index < 4; ++index)
	{
		panels[index] = new QWidget(this);
		panels[index]->setObjectName(QStringLiteral("debuggerPanel%1").arg(index));
	}
	auto *cpuLayout = new QVBoxLayout(panels[0]);
	auto *cpuRow = new QHBoxLayout;
	auto *registerColumn = new QVBoxLayout;
	auto *registerGrid = new QGridLayout;
	registerGrid->setHorizontalSpacing(5);
	const QString regNames[] = {QStringLiteral("PC"), QStringLiteral("A"), QStringLiteral("X"), QStringLiteral("Y"),
		QStringLiteral("SP"), QStringLiteral("P")};
	for (int index = 0; index < 6; ++index)
	{
		const int width = index == 0 ? 4 : 2;
		auto *edit = dataField(panels[0], QStringLiteral("debuggerRegister%1").arg(regNames[index]), width, false);
		edit->setMaxLength(width);
		edit->setValidator(new QRegularExpressionValidator(
			QRegularExpression(QStringLiteral("[0-9a-fA-F]{%1}").arg(width)), edit));
		registerEdits[index] = edit;
		auto *row = new QHBoxLayout;
		row->setSpacing(3);
		row->addWidget(new QLabel(regNames[index] + QLatin1Char(':'), panels[0]));
		row->addWidget(edit);
		registerGrid->addLayout(row, index / 4, index % 4);
		connect(edit, &QLineEdit::editingFinished, this, [this, index]() { applyRegister(index); });
	}
	registerColumn->addLayout(registerGrid);
	auto *flagGroup = new QGroupBox(tr("Status Flags"), panels[0]);
	auto *flagGrid = new QGridLayout(flagGroup);
	const char flags[] = {'N', 'V', 'U', 'B', 'D', 'I', 'Z', 'C'};
	const QString descriptions[] = {tr("Negative"), tr("Overflow"), tr("Unused"), tr("Break"), tr("Decimal"),
		tr("Interrupt disable"), tr("Zero"), tr("Carry")};
	for (int index = 0; index < 8; ++index)
	{
		auto *check = new QCheckBox(QString(QChar::fromLatin1(flags[index])), flagGroup);
		check->setObjectName(QStringLiteral("debuggerFlag%1").arg(QChar::fromLatin1(flags[index])));
		check->setToolTip(descriptions[index]);
		flagEdits[index] = check;
		flagGrid->addWidget(check, index / 4, index % 4);
		connect(check, &QCheckBox::toggled, this, [this, index](bool value) { applyFlag(7 - index, value); });
	}
	registerColumn->addWidget(flagGroup);
	auto *counterGrid = new QFormLayout;
	cyclesField = dataField(panels[0], QStringLiteral("debuggerCpuCycles"), 12);
	instructionsField = dataField(panels[0], QStringLiteral("debuggerInstructions"), 12);
	counterGrid->addRow(tr("CPU Cycles:"), cyclesField);
	counterGrid->addRow(tr("Instructions:"), instructionsField);
	registerColumn->addLayout(counterGrid);
	registerColumn->addStretch();
	cpuRow->addLayout(registerColumn, 3);
	auto *stackGroup = new QGroupBox(tr("Stack $0100"), panels[0]);
	auto *stackLayout = new QVBoxLayout(stackGroup);
	stackText->setFont(fixed);
	stackText->setReadOnly(true);
	stackText->setLineWrapMode(QPlainTextEdit::NoWrap);
	stackText->setMinimumWidth(120);
	stackLayout->addWidget(stackText);
	cpuRow->addWidget(stackGroup, 1);
	cpuLayout->addLayout(cpuRow);
	cpuLayout->setContentsMargins(5, 5, 5, 5);

	auto *ppuLayout = new QGridLayout(panels[1]);
	const QString labels[] = {tr("PPUCTRL:"), tr("PPUMASK:"), tr("PPUSTAT:"), tr("OAMADDR:"), tr("PPUADDR (v):"),
		tr("Temp (t):"), tr("Scanline:"), tr("Pixel:"), tr("Fine X:"), tr("Write Toggle:"), tr("Frame Lines:")};
	for (int index = 0; index < 11; ++index)
	{
		ppuFields[index] = dataField(panels[1], QStringLiteral("debuggerPpu%1").arg(index), index == 4 || index == 5 ? 4 : 3);
		ppuLayout->addWidget(new QLabel(labels[index], panels[1]), index / 3, (index % 3) * 2);
		ppuLayout->addWidget(ppuFields[index], index / 3, (index % 3) * 2 + 1);
	}
	auto *ppuNote = new QLabel(tr("Registers reflect the last PPU synchronization; inspection does not advance it."), panels[1]);
	ppuNote->setWordWrap(true);
	ppuLayout->addWidget(ppuNote, 4, 0, 1, 6);
	ppuLayout->setRowStretch(5, 1);

	auto *bpLayout = new QVBoxLayout(panels[2]);
	breakpointTable->setObjectName(QStringLiteral("debuggerBreakpoints"));
	breakpointTable->setColumnCount(4);
	breakpointTable->setHorizontalHeaderLabels({tr("CPU Address"), tr("Type"), tr("Condition"), tr("Mapped PRG Offset")});
	breakpointTable->setFont(fixed);
	breakpointTable->verticalHeader()->hide();
	breakpointTable->setShowGrid(false);
	breakpointTable->setAlternatingRowColors(true);
	breakpointTable->setSelectionBehavior(QAbstractItemView::SelectRows);
	breakpointTable->setSelectionMode(QAbstractItemView::SingleSelection);
	breakpointTable->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
	breakpointTable->horizontalHeader()->setStretchLastSection(true);
	bpLayout->addWidget(breakpointTable, 1);
	auto *bpButtons = new QHBoxLayout;
	auto *add = new QPushButton(tr("Add"), panels[2]);
	add->setObjectName(QStringLiteral("debuggerAddBreakpoint"));
	auto *edit = new QPushButton(tr("Edit"), panels[2]);
	edit->setObjectName(QStringLiteral("debuggerEditBreakpoint"));
	auto *remove = new QPushButton(tr("Delete"), panels[2]);
	remove->setObjectName(QStringLiteral("debuggerRemoveBreakpoint"));
	for (auto *button : {add, edit, remove})
	{
		button->setAutoDefault(false);
		bpButtons->addWidget(button);
	}
	bpLayout->addLayout(bpButtons);
	connect(add, &QPushButton::clicked, this, &DebuggerWindow::addBreakpoint);
	connect(edit, &QPushButton::clicked, this, &DebuggerWindow::editBreakpoint);
	connect(remove, &QPushButton::clicked, this, &DebuggerWindow::removeBreakpoint);
	connect(breakpointTable, &QTableWidget::itemChanged, this, &DebuggerWindow::onBreakpointChanged);
	connect(breakpointTable, &QTableWidget::cellDoubleClicked, this, [this](int, int) { editBreakpoint(); });
	breakpointTable->setToolTip(tr("Double-click a row to edit the address and condition"));

	auto *bmLayout = new QVBoxLayout(panels[3]);
	bookmarkList->setObjectName(QStringLiteral("debuggerBookmarks"));
	bookmarkList->setFont(fixed);
	bookmarkList->setAlternatingRowColors(true);
	bmLayout->addWidget(bookmarkList, 1);
	auto *bmButtons = new QHBoxLayout;
	auto *bmAdd = new QPushButton(tr("Add"), panels[3]);
	auto *bmName = new QPushButton(tr("Name"), panels[3]);
	auto *bmRemove = new QPushButton(tr("Delete"), panels[3]);
	bmAdd->setObjectName(QStringLiteral("debuggerAddBookmark"));
	bmRemove->setObjectName(QStringLiteral("debuggerRemoveBookmark"));
	for (auto *button : {bmAdd, bmName, bmRemove})
	{
		button->setAutoDefault(false);
		bmButtons->addWidget(button);
	}
	bmLayout->addLayout(bmButtons);
	connect(bmAdd, &QPushButton::clicked, this, &DebuggerWindow::addBookmark);
	connect(bmRemove, &QPushButton::clicked, this, &DebuggerWindow::removeBookmark);
	connect(bmName, &QPushButton::clicked, this, [this]() {
		auto *item = bookmarkList->currentItem();
		if (item == nullptr)
		{
			return;
		}
		const uint address = item->data(Qt::UserRole).toUInt();
		bool ok = false;
		const QString name = QInputDialog::getText(this, tr("Bookmark Name"), addressText(address), QLineEdit::Normal,
			bookmarks.value(address), &ok);
		if (ok)
		{
			bookmarks[address] = name;
			updateBookmarks();
		}
	});
	connect(bookmarkList, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem *item) {
		disassembly->goToAddress(uint16_t(item->data(Qt::UserRole).toUInt()));
	});
}

void DebuggerWindow::setLayoutPreset(int preset)
{
	layoutPreset = qBound(1, preset, 4);
	for (auto &column : tabs)
	{
		for (auto *group : column)
		{
			while (group->count() > 0)
			{
				group->removeTab(0);
			}
		}
	}
	const QString titles[] = {tr("CPU"), tr("PPU"), tr("Breakpoints"), tr("Bookmarks")};
	for (int index = 0; index < 4; ++index)
	{
		int column = layoutPreset >= 3 && index >= 2 ? 1 : 0;
		int row = 0;
		if (layoutPreset == 2)
		{
			row = index >= 2 ? 1 : 0;
		}
		else if (layoutPreset == 3)
		{
			row = index == 1 ? 1 : 0;
		}
		else if (layoutPreset == 4)
		{
			row = index % 2;
		}
		tabs[column][row]->addTab(panels[index], titles[index]);
	}
	presetActions[layoutPreset - 1]->setChecked(true);
	updateTabVisibility();
	horizontal->setSizes({540, 460, 400});
	for (auto *column : columns)
	{
		column->setSizes({280, 280, 140, 140});
	}
}

void DebuggerWindow::movePanel(QWidget *panel, int column, int row)
{
	for (auto &groups : tabs)
	{
		for (auto *source : groups)
		{
			const int index = source->indexOf(panel);
			if (index >= 0)
			{
				const QString title = source->tabText(index);
				source->removeTab(index);
				tabs[column][row]->setCurrentIndex(tabs[column][row]->addTab(panel, title));
				updateTabVisibility();
				horizontal->setSizes({540, 460, 400});
				columns[column]->setSizes({240, 240, 240, 240});
				return;
			}
		}
	}
}

void DebuggerWindow::updateTabVisibility(void)
{
	for (int column = 0; column < 2; ++column)
	{
		bool visible = false;
		for (auto *group : tabs[column])
		{
			group->setVisible(group->count() > 0);
			visible |= group->count() > 0;
		}
		columns[column]->setVisible(visible);
	}
}

void DebuggerWindow::saveLayout(void)
{
	QSettings settings;
	settings.beginGroup(QStringLiteral("debugger"));
	settings.setValue(QStringLiteral("geometry"), saveGeometry());
	settings.setValue(QStringLiteral("preset"), layoutPreset);
	settings.setValue(QStringLiteral("horizontal"), horizontal->saveState());
	settings.setValue(QStringLiteral("font"), disassembly->font().toString());
	settings.setValue(QStringLiteral("pcCentered"), findChild<QAction *>(QStringLiteral("debuggerPCCentered"))->isChecked());
	settings.setValue(QStringLiteral("showBytes"), findChild<QAction *>(QStringLiteral("debuggerShowBytes"))->isChecked());
	settings.setValue(QStringLiteral("showRomOffsets"), findChild<QAction *>(QStringLiteral("debuggerShowRomOffsets"))->isChecked());
	for (int column = 0; column < 2; ++column)
	{
		settings.setValue(QStringLiteral("column%1").arg(column), columns[column]->saveState());
		for (int row = 0; row < 4; ++row)
		{
			auto *group = tabs[column][row];
			QStringList pages;
			for (int index = 0; index < group->count(); ++index)
			{
				pages.append(group->widget(index)->objectName());
			}
			settings.setValue(QStringLiteral("tabs%1%2").arg(column).arg(row), pages);
			settings.setValue(QStringLiteral("current%1%2").arg(column).arg(row), group->currentIndex());
		}
	}
}

void DebuggerWindow::restoreLayout(void)
{
	QSettings settings;
	settings.beginGroup(QStringLiteral("debugger"));
	setLayoutPreset(settings.value(QStringLiteral("preset"), 2).toInt());
	QSet<QString> pages;
	bool valid = true;
	for (int column = 0; column < 2; ++column)
	{
		for (int row = 0; row < 4; ++row)
		{
			for (const QString &name : settings.value(QStringLiteral("tabs%1%2").arg(column).arg(row)).toStringList())
			{
				bool known = false;
				for (auto *panel : panels)
				{
					known |= panel->objectName() == name;
				}
				valid &= known && !pages.contains(name);
				pages.insert(name);
			}
		}
	}
	if (valid && pages.size() == 4)
	{
		for (auto &column : tabs)
		{
			for (auto *group : column)
			{
				while (group->count() > 0)
				{
					group->removeTab(0);
				}
			}
		}
		const QString titles[] = {tr("CPU"), tr("PPU"), tr("Breakpoints"), tr("Bookmarks")};
		for (int column = 0; column < 2; ++column)
		{
			for (int row = 0; row < 4; ++row)
			{
				auto *group = tabs[column][row];
				for (const QString &name : settings.value(QStringLiteral("tabs%1%2").arg(column).arg(row)).toStringList())
				{
					for (int index = 0; index < 4; ++index)
					{
						if (panels[index]->objectName() == name)
						{
							group->addTab(panels[index], titles[index]);
						}
					}
				}
				group->setCurrentIndex(settings.value(QStringLiteral("current%1%2").arg(column).arg(row), 0).toInt());
			}
		}
		updateTabVisibility();
	}
	if (settings.contains(QStringLiteral("geometry")))
	{
		restoreGeometry(settings.value(QStringLiteral("geometry")).toByteArray());
	}
	if (settings.contains(QStringLiteral("horizontal")))
	{
		horizontal->restoreState(settings.value(QStringLiteral("horizontal")).toByteArray());
	}
	for (int column = 0; column < 2; ++column)
	{
		if (settings.contains(QStringLiteral("column%1").arg(column)))
		{
			columns[column]->restoreState(settings.value(QStringLiteral("column%1").arg(column)).toByteArray());
		}
	}
	QFont font;
	const QString fontSetting = settings.value(QStringLiteral("font")).toString();
	if (!fontSetting.isEmpty() && font.fromString(fontSetting))
	{
		disassembly->setFont(font);
	}
}

void DebuggerWindow::showEvent(QShowEvent *event)
{
	QDialog::showEvent(event);
	if (!emulation.isDebuggerEnabled())
	{
		emulation.setDebuggerEnabled(true);
	}
	snapshotDirty = true;
	followPC = true;
	refreshTimer->start();
	refresh();
}

void DebuggerWindow::machineChanged(void)
{
	snapshotDirty = true;
	followPC = true;
	refresh();
}

void DebuggerWindow::refresh(void)
{
	if (!isVisible())
	{
		return;
	}
	bool loaded;
	bool halted;
	Registers registers = {};
	Nes::Api::Debugger::PpuState ppu = {};
	Nes::qaword cycles = 0, instructions = 0;
	std::unique_ptr<DisassemblyView::Snapshot> snapshot;
	bool captured = false;
	QString stack;
	{
		EmulationThread::Locker lock(emulation.mutex());
		auto &core = emulation.core();
		loaded = core.isLoaded();
		halted = emulation.isHalted();
		if (loaded)
		{
			auto &debugger = core.debugger();
			debugger.GetRegisters(registers);
			debugger.GetPpuState(ppu);
			cycles = debugger.GetCpuCycles();
			instructions = debugger.GetInstructionCount();
			if (snapshotDirty || !hadImage)
			{
				snapshot = std::make_unique<DisassemblyView::Snapshot>();
				for (uint address = 0; address < 0x10000; ++address)
				{
					snapshot->bytes[address] = debugger.Peek(address);
					snapshot->prgOffsets[address] = debugger.GetPrgRomOffset(address);
				}
				captured = true;
			}
			for (uint offset = 1; offset <= 0x100; ++offset)
			{
				const uint address = 0x100 | ((registers.sp + offset) & 0xFF);
				stack += QStringLiteral("%1: %2\n").arg(hex(address, 4), hex(uint(debugger.Peek(address))));
			}
			for (int row = 0; row < breakpointTable->rowCount(); ++row)
			{
				const uint address = breakpointTable->item(row, 0)->data(Qt::UserRole).toUInt();
				const long offset = debugger.GetPrgRomOffset(address);
				breakpointTable->item(row, 3)->setText(offset < 0 ? QStringLiteral("--") : hex(qulonglong(offset), 6));
			}
		}
	}
	updating = true;
	hadImage = loaded;
	if (loaded)
	{
		if (captured)
		{
			disassembly->setSnapshot(*snapshot, uint16_t(registers.pc), followPC);
			snapshotDirty = false;
			followPC = false;
		}
		const uint values[] = {registers.pc, registers.a, registers.x, registers.y, registers.sp, registers.p};
		for (int index = 0; index < 6; ++index)
		{
			auto *edit = registerEdits[index];
			edit->setReadOnly(!halted);
			if (!edit->hasFocus() || !halted || !edit->isModified())
			{
				edit->setText(hex(values[index], index == 0 ? 4 : 2));
			}
		}
		for (int index = 0; index < 8; ++index)
		{
			flagEdits[index]->setEnabled(halted);
			flagEdits[index]->setChecked((registers.p & (1u << (7 - index))) != 0);
		}
		const QString ppuValues[] = {hex(ppu.ctrl), hex(ppu.mask), hex(ppu.status), hex(ppu.oamAddress),
			hex(ppu.vramAddress, 4), hex(ppu.tempAddress, 4), QString::number(ppu.scanline), QString::number(ppu.dot),
			QString::number(ppu.fineX), QString::number(ppu.writeToggle), QString::number(ppu.linesPerFrame)};
		for (int index = 0; index < 11; ++index)
		{
			ppuFields[index]->setText(ppuValues[index]);
		}
		const uint bits[] = {ppu.ctrl, ppu.mask, ppu.status};
		for (int index = 0; index < 3; ++index)
		{
			ppuFields[index]->setToolTip(tr("Binary: %1").arg(QString::number(bits[index], 2).rightJustified(8, QLatin1Char('0'))));
		}
		cyclesField->setText(QString::number(qulonglong(cycles)));
		instructionsField->setText(QString::number(qulonglong(instructions)));
		if (stackText->toPlainText() != stack.trimmed())
		{
			stackText->setPlainText(stack.trimmed());
		}
		statusLabel->setText(halted ? tr("Halted at %1").arg(addressText(registers.pc)) :
			emulation.isPaused() ? tr("Emulation paused") : tr("Running - assembly shows the last captured snapshot"));
	}
	else
	{
		disassembly->clear();
		stackText->clear();
		for (int index = 0; index < 6; ++index)
		{
			registerEdits[index]->clear();
			registerEdits[index]->setReadOnly(true);
		}
		for (auto *flag : flagEdits)
		{
			flag->setChecked(false);
			flag->setEnabled(false);
		}
		for (auto *field : ppuFields)
		{
			field->clear();
		}
		cyclesField->clear();
		instructionsField->clear();
		statusLabel->setText(tr("Open a ROM to use the debugger"));
	}
	for (auto *action : haltActions)
	{
		action->setEnabled(loaded && halted);
	}
	breakAction->setEnabled(loaded && !halted && !emulation.isPaused());
	for (auto *panel : panels)
	{
		panel->setEnabled(loaded);
	}
	updating = false;
}

void DebuggerWindow::resume(Request::Mode mode)
{
	if (!commitRegisters())
	{
		return;
	}
	if (!emulation.resume({mode, 0, 0}))
	{
		setStatus(tr("The machine must be halted before using this command"));
		return;
	}
	messageLabel->clear();
	for (auto *action : haltActions)
	{
		action->setEnabled(false);
	}
}

void DebuggerWindow::runToAddress(uint address)
{
	if (!commitRegisters())
	{
		return;
	}
	if (!emulation.resume({Request::RUN_TO_ADDRESS, address, 0}))
	{
		setStatus(tr("Halt the machine before running to an address"));
		return;
	}
	messageLabel->clear();
	for (auto *action : haltActions)
	{
		action->setEnabled(false);
	}
}

void DebuggerWindow::goToAddress(void)
{
	bool ok = false;
	const QString text = QInputDialog::getText(this, tr("Go to Address"), tr("Hexadecimal CPU address:"),
		QLineEdit::Normal, addressText(disassembly->selectedAddress()), &ok);
	uint address = 0;
	if (!ok)
	{
		return;
	}
	if (!BreakpointDialog::parseAddress(text, address))
	{
		setStatus(tr("Enter a hexadecimal CPU address from $0000 to $FFFF"));
		return;
	}
	disassembly->goToAddress(uint16_t(address));
}

bool DebuggerWindow::commitRegisters(void)
{
	for (int index = 0; index < int(registerEdits.size()); ++index)
	{
		if (!applyRegister(index))
		{
			return false;
		}
	}
	return true;
}

bool DebuggerWindow::applyRegister(int index)
{
	if (updating || !registerEdits[index]->isModified())
	{
		return true;
	}
	auto *edit = registerEdits[index];
	if (!edit->hasAcceptableInput())
	{
		setStatus(tr("Enter a complete hexadecimal register value"));
		return false;
	}
	bool ok = false;
	const uint value = edit->text().toUInt(&ok, 16);
	if (!ok)
	{
		setStatus(tr("Invalid register value"));
		return false;
	}
	{
		EmulationThread::Locker lock(emulation.mutex());
		if (!emulation.isHalted())
		{
			setStatus(tr("Registers can only be edited while halted"));
			return false;
		}
		auto &debugger = emulation.core().debugger();
		Registers registers;
		debugger.GetRegisters(registers);
		switch (index)
		{
			case 0: registers.pc = value; break;
			case 1: registers.a = value; break;
			case 2: registers.x = value; break;
			case 3: registers.y = value; break;
			case 4: registers.sp = value; break;
			case 5: registers.p = value; break;
		}
		debugger.SetRegisters(registers);
	}
	edit->setModified(false);
	snapshotDirty = true;
	followPC = index == 0;
	refresh();
	return true;
}

void DebuggerWindow::applyFlag(int bit, bool checked)
{
	if (updating)
	{
		return;
	}
	{
		EmulationThread::Locker lock(emulation.mutex());
		if (!emulation.isHalted())
		{
			setStatus(tr("Flags can only be edited while halted"));
			return;
		}
		auto &debugger = emulation.core().debugger();
		Registers registers;
		debugger.GetRegisters(registers);
		registers.p = checked ? registers.p | (1u << bit) : registers.p & ~(1u << bit);
		debugger.SetRegisters(registers);
	}
	refresh();
}

Nes::Result DebuggerWindow::applyBreakpoint(uint address, bool enabled, const QString &condition)
{
	const QByteArray text = condition.toUtf8();
	EmulationThread::Locker lock(emulation.mutex());
	return emulation.core().debugger().SetExecBreakpoint(address, enabled, text.constData());
}

void DebuggerWindow::addBreakpoint(void)
{
	openBreakpointDialog(disassembly->selectedAddress(), false);
}

void DebuggerWindow::setBreakpoint(uint address, bool enabled)
{
	if (applyBreakpoint(address, enabled, conditions.value(address)) != Nes::RESULT_OK)
	{
		setStatus(tr("The core rejected breakpoint %1").arg(addressText(address)));
		return;
	}
	breakpoints[address] = enabled;
	updateBreakpoints();
}

// editing selects between changing the breakpoint at address and adding one prefilled with it
void DebuggerWindow::openBreakpointDialog(uint address, bool editing)
{
	BreakpointDialog::Values initial;
	initial.address = address;
	QSet<uint> occupied;
	for (auto it = breakpoints.cbegin(); it != breakpoints.cend(); ++it)
	{
		if (editing && it.key() == address)
		{
			initial.enabled = it.value();
			initial.condition = conditions.value(address);
		}
		else
		{
			occupied.insert(it.key());
		}
	}
	BreakpointDialog dialog(initial, editing, occupied, this);
	if (dialog.exec() != QDialog::Accepted)
	{
		return;
	}
	const BreakpointDialog::Values values = dialog.values();
	Nes::Result result = applyBreakpoint(values.address, values.enabled, values.condition);
	const bool moved = editing && values.address != address;
	if (result == Nes::RESULT_OK && moved)
	{
		result = applyBreakpoint(address, false, QString());
	}
	if (result != Nes::RESULT_OK)
	{
		setStatus(tr("Unable to set the execute breakpoint"));
		return;
	}
	if (moved)
	{
		breakpoints.remove(address);
		conditions.remove(address);
	}
	breakpoints[values.address] = values.enabled;
	if (values.condition.isEmpty())
	{
		conditions.remove(values.address);
	}
	else
	{
		conditions[values.address] = values.condition;
	}
	updateBreakpoints();
}

void DebuggerWindow::editBreakpoint(void)
{
	const int row = breakpointTable->currentRow();
	if (row < 0)
	{
		setStatus(tr("Select a breakpoint to edit"));
		return;
	}
	openBreakpointDialog(breakpointTable->item(row, 0)->data(Qt::UserRole).toUInt(), true);
}

void DebuggerWindow::removeBreakpoint(void)
{
	const int row = breakpointTable->currentRow();
	if (row < 0)
	{
		setStatus(tr("Select a breakpoint to delete"));
		return;
	}
	const uint address = breakpointTable->item(row, 0)->data(Qt::UserRole).toUInt();
	if (applyBreakpoint(address, false, QString()) != Nes::RESULT_OK)
	{
		setStatus(tr("Unable to delete the execute breakpoint"));
		return;
	}
	breakpoints.remove(address);
	conditions.remove(address);
	updateBreakpoints();
}

void DebuggerWindow::onBreakpointChanged(QTableWidgetItem *item)
{
	if (updating || item == nullptr || item->column() != 0)
	{
		return;
	}
	const uint address = item->data(Qt::UserRole).toUInt();
	const bool enabled = item->checkState() == Qt::Checked;
	if (applyBreakpoint(address, enabled, conditions.value(address)) != Nes::RESULT_OK)
	{
		QSignalBlocker blocker(breakpointTable);
		item->setCheckState(breakpoints.value(address) ? Qt::Checked : Qt::Unchecked);
		setStatus(tr("Unable to change the execute breakpoint"));
		return;
	}
	breakpoints[address] = enabled;
	disassembly->setBreakpoints(breakpoints, QSet<uint>(conditions.keyBegin(), conditions.keyEnd()));
}

void DebuggerWindow::updateBreakpoints(void)
{
	const int row = breakpointTable->currentRow();
	const uint selected = row >= 0 ? breakpointTable->item(row, 0)->data(Qt::UserRole).toUInt() : 0x10000;
	QSignalBlocker blocker(breakpointTable);
	breakpointTable->setRowCount(breakpoints.size());
	int index = 0;
	for (auto it = breakpoints.cbegin(); it != breakpoints.cend(); ++it, ++index)
	{
		auto *item = tableItem(addressText(it.key()));
		item->setData(Qt::UserRole, it.key());
		item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
		item->setCheckState(it.value() ? Qt::Checked : Qt::Unchecked);
		breakpointTable->setItem(index, 0, item);
		breakpointTable->setItem(index, 1, tableItem(tr("Execute")));
		breakpointTable->setItem(index, 2, tableItem(conditions.value(it.key())));
		breakpointTable->setItem(index, 3, tableItem(QStringLiteral("--")));
		if (it.key() == selected)
		{
			breakpointTable->selectRow(index);
		}
	}
	disassembly->setBreakpoints(breakpoints, QSet<uint>(conditions.keyBegin(), conditions.keyEnd()));
}

void DebuggerWindow::addBookmark(void)
{
	addBookmarkAt(disassembly->selectedAddress());
}

void DebuggerWindow::addBookmarkAt(uint address)
{
	if (!bookmarks.contains(address))
	{
		bookmarks.insert(address, {});
	}
	updateBookmarks();
	setStatus(tr("Bookmark added at %1").arg(addressText(address)));
}

void DebuggerWindow::removeBookmark(void)
{
	auto *item = bookmarkList->currentItem();
	if (item == nullptr)
	{
		setStatus(tr("Select a bookmark to delete"));
		return;
	}
	bookmarks.remove(item->data(Qt::UserRole).toUInt());
	updateBookmarks();
}

void DebuggerWindow::updateBookmarks(void)
{
	bookmarkList->clear();
	for (auto it = bookmarks.cbegin(); it != bookmarks.cend(); ++it)
	{
		auto *item = new QListWidgetItem(addressText(it.key()) + QStringLiteral("  ") + it.value(), bookmarkList);
		item->setData(Qt::UserRole, it.key());
	}
}

void DebuggerWindow::setStatus(const QString &message)
{
	messageLabel->setText(message);
}
