#include "debugger/DisassemblyView.h"

#include <algorithm>

#include <QApplication>
#include <QClipboard>
#include <QContextMenuEvent>
#include <QFontDatabase>
#include <QKeyEvent>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QScrollBar>
#include <QWheelEvent>

namespace
{
	QString hex(uint value, int width)
	{
		return QStringLiteral("%1").arg(value, width, 16, QLatin1Char('0')).toUpper();
	}
}

DisassemblyView::DisassemblyView(QWidget *parent)
	: QAbstractScrollArea(parent)
{
	setObjectName(QStringLiteral("debuggerDisassembly"));
	setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
	setFocusPolicy(Qt::StrongFocus);
	viewport()->setMouseTracking(true);
	viewport()->setAutoFillBackground(false);
	setMinimumSize(360, 240);
	verticalScrollBar()->setRange(0, 0xFFFF);
	horizontalScrollBar()->setSingleStep(charWidth());
	connect(verticalScrollBar(), &QScrollBar::valueChanged, this, [this]() { rebuildLines(); });
	connect(verticalScrollBar(), &QScrollBar::actionTriggered, this, [this](int action) {
		int address = verticalScrollBar()->value();
		int count = 0;
		switch (action)
		{
			case QAbstractSlider::SliderSingleStepAdd: count = 1; break;
			case QAbstractSlider::SliderSingleStepSub: count = -1; break;
			case QAbstractSlider::SliderPageStepAdd: count = std::max(1, int(lines.size()) * 3 / 4); break;
			case QAbstractSlider::SliderPageStepSub: count = -std::max(1, int(lines.size()) * 3 / 4); break;
			default: return;
		}
		for (int index = 0; index < std::abs(count); ++index)
		{
			address = count > 0 ? nextAddress(address) : previousAddress(address);
		}
		verticalScrollBar()->setSliderPosition(address);
	});
	connect(horizontalScrollBar(), &QScrollBar::valueChanged, viewport(), qOverload<>(&QWidget::update));
	snapshot.bytes.fill(-1);
	snapshot.prgOffsets.fill(-1);
}

void DisassemblyView::setSnapshot(const Snapshot &value, uint16_t address, bool followPC)
{
	snapshot = value;
	predecessors.clear();
	pc = address;
	available = true;
	rebuildLines();

	if (followPC)
	{
		selection = selectionAnchor = pc;
		operandTarget = -1;
		reveal(pc);
	}
}

void DisassemblyView::clear(void)
{
	available = false;
	lines.clear();
	predecessors.clear();
	backHistory.clear();
	forwardHistory.clear();
	viewport()->update();
}

void DisassemblyView::setBreakpoints(const QMap<uint, bool> &value)
{
	breakpoints = value;
	viewport()->update();
}

uint16_t DisassemblyView::topAddress(void) const
{
	return uint16_t(verticalScrollBar()->value());
}

int DisassemblyView::lineHeight(void) const
{
	return fontMetrics().height() + 2;
}

int DisassemblyView::charWidth(void) const
{
	return fontMetrics().horizontalAdvance(QLatin1Char('0'));
}

int DisassemblyView::instructionColumn(void) const
{
	return 4 + (showRomOffsets ? 10 : 6) + (showBytes ? 10 : 0);
}

Disassembler::Instruction DisassemblyView::instructionAt(uint16_t address) const
{
	auto instruction = Disassembler::disassemble(address, [this](uint16_t at) { return snapshot.bytes[at]; });
	// The PC and explicit navigation target are known instruction boundaries;
	// preceding bytes may be data rather than code.
	for (uint16_t boundary : {pc, selection})
	{
		if (address < boundary && unsigned(address) + unsigned(instruction.length) > boundary)
		{
			instruction.length = boundary - address;
			instruction.valid = false;
			instruction.text = QStringLiteral("???");
		}
	}
	return instruction;
}

int DisassemblyView::nextAddress(int address) const
{
	return std::min(0xFFFF, address + instructionAt(uint16_t(address)).length);
}

int DisassemblyView::previousAddress(int address) const
{
	const auto known = predecessors.constFind(address);
	if (known != predecessors.cend() && nextAddress(known.value()) == address)
	{
		return known.value();
	}
	// 6502 code cannot be decoded backwards unambiguously. Prefer a complete,
	// readable instruction ending here, otherwise move back one byte.
	for (int length = 3; length >= 1; --length)
	{
		if (address >= length)
		{
			const auto instruction = instructionAt(uint16_t(address - length));
			if (instruction.valid && instruction.length == length)
			{
				return address - length;
			}
		}
	}
	return std::max(0, address - 1);
}

void DisassemblyView::rebuildLines(void)
{
	lines.clear();
	const int rows = viewport()->height() / lineHeight() + 1;
	verticalScrollBar()->setPageStep(std::max(1, rows * 2));
	horizontalScrollBar()->setRange(0, std::max(0, (instructionColumn() + 22) * charWidth() - viewport()->width()));

	if (available)
	{
		int address = topAddress();
		for (int row = 0; row < rows; ++row)
		{
			const auto instruction = instructionAt(uint16_t(address));
			lines.append(instruction);
			predecessors.insert(address + instruction.length, address);
			address += instruction.length;
			if (address > 0xFFFF)
			{
				break;
			}
		}
	}
	viewport()->update();
}

void DisassemblyView::reveal(uint16_t address)
{
	int top = address;
	if (centerPC)
	{
		const int above = std::max(0, viewport()->height() / lineHeight() / 2);
		for (int row = 0; row < above; ++row)
		{
			top = previousAddress(top);
		}
	}
	verticalScrollBar()->setValue(top);
	rebuildLines();
}

void DisassemblyView::goToAddress(uint16_t address)
{
	if (!available)
	{
		return;
	}
	backHistory.append(selection);
	forwardHistory.clear();
	selection = selectionAnchor = address;
	operandTarget = -1;
	reveal(address);
	// An explicit navigation address is also an instruction boundary.
	if (std::none_of(lines.cbegin(), lines.cend(), [address](const auto &line) { return line.address == address; }))
	{
		verticalScrollBar()->setValue(address);
	}
}

void DisassemblyView::goToPC(void)
{
	goToAddress(pc);
}

void DisassemblyView::navigateBack(void)
{
	if (!backHistory.isEmpty())
	{
		forwardHistory.append(selection);
		selection = selectionAnchor = backHistory.takeLast();
		reveal(selection);
	}
}

void DisassemblyView::navigateForward(void)
{
	if (!forwardHistory.isEmpty())
	{
		backHistory.append(selection);
		selection = selectionAnchor = forwardHistory.takeLast();
		reveal(selection);
	}
}

void DisassemblyView::setShowBytes(bool show)
{
	showBytes = show;
	rebuildLines();
}

void DisassemblyView::setShowRomOffsets(bool show)
{
	showRomOffsets = show;
	rebuildLines();
}

void DisassemblyView::setPCPlacement(bool center)
{
	centerPC = center;
	goToPC();
}

QString DisassemblyView::lineText(const Disassembler::Instruction &instruction) const
{
	QString text = QStringLiteral("$") + hex(instruction.address, 4) + QStringLiteral(": ");
	if (showBytes)
	{
		QString bytes;
		for (int i = 0; i < instruction.length; ++i)
		{
			const int value = snapshot.bytes[uint16_t(instruction.address + i)];
			bytes += (value < 0 ? QStringLiteral("??") : hex(uint(value), 2)) + QLatin1Char(' ');
		}
		text += bytes.leftJustified(10);
	}
	return text + instruction.text;
}

QString DisassemblyView::selectedText(void) const
{
	if (!available)
	{
		return {};
	}
	QStringList text;
	int address = std::min(selection, selectionAnchor);
	const int last = std::max(selection, selectionAnchor);
	while (address <= last)
	{
		const auto instruction = instructionAt(uint16_t(address));
		text.append(lineText(instruction));
		address += instruction.length;
	}
	return text.join(QLatin1Char('\n'));
}

void DisassemblyView::copySelection(void) const
{
	QApplication::clipboard()->setText(selectedText());
}

void DisassemblyView::paintEvent(QPaintEvent *)
{
	QPainter painter(viewport());
	painter.setFont(font());
	const int cw = charWidth();
	const int lh = lineHeight();
	const int shift = horizontalScrollBar()->value();
	painter.fillRect(viewport()->rect(), palette().base());
	painter.fillRect(0, 0, 4 * cw, viewport()->height(), palette().alternateBase());
	const bool dark = palette().color(QPalette::Base).lightness() < 128;
	const QColor opcodeColor = dark ? QColor(120, 195, 255) : QColor(0, 65, 160);
	const QColor operandColor = dark ? QColor(155, 215, 165) : QColor(0, 105, 50);

	for (int row = 0; row < lines.size(); ++row)
	{
		const auto &instruction = lines[row];
		const int top = row * lh;
		const int baseline = top + fontMetrics().ascent() + 1;
		const bool selected = instruction.address >= std::min(selection, selectionAnchor) &&
			instruction.address <= std::max(selection, selectionAnchor);
		if (instruction.address == pc)
		{
			painter.fillRect(4 * cw, top, viewport()->width(), lh,
				dark ? QColor(80, 65, 25) : QColor(255, 246, 190));
		}
		if (selected && hasFocus())
		{
			painter.fillRect(4 * cw, top, viewport()->width(), lh, palette().highlight());
		}
		if (breakpoints.contains(instruction.address))
		{
			painter.setPen(QColor(185, 35, 40));
			painter.setBrush(breakpoints.value(instruction.address) ? QBrush(QColor(215, 50, 50)) : Qt::NoBrush);
			painter.drawEllipse(QRect(cw / 2, top + 3, cw, std::max(4, lh - 6)));
		}
		if (instruction.address == pc)
		{
			painter.setPen(Qt::NoPen);
			painter.setBrush(QColor(215, 50, 50));
			painter.drawPolygon(QPolygon({QPoint(2 * cw, top + 3), QPoint(2 * cw, top + lh - 3),
				QPoint(3 * cw + cw / 2, top + lh / 2)}));
		}
		painter.save();
		painter.setClipRect(4 * cw, top, viewport()->width() - 4 * cw, lh);
		const QColor normal = selected && hasFocus() ? palette().color(QPalette::HighlightedText) :
			palette().color(QPalette::Text);
		painter.setPen(normal);
		int column = 4;
		QString address = hex(instruction.address, 4) + QStringLiteral(": ");
		if (showRomOffsets)
		{
			const long offset = snapshot.prgOffsets[instruction.address];
			address = offset < 0 ? QStringLiteral("------:   ") : hex(uint(offset), 6) + QStringLiteral(":   ");
		}
		painter.drawText(column * cw - shift, baseline, address);
		column += showRomOffsets ? 10 : 6;
		if (showBytes)
		{
			QString bytes;
			for (int i = 0; i < instruction.length; ++i)
			{
				const int value = snapshot.bytes[uint16_t(instruction.address + i)];
				bytes += (value < 0 ? QStringLiteral("??") : hex(uint(value), 2)) + QLatin1Char(' ');
			}
			painter.drawText(column * cw - shift, baseline, bytes);
			column += 10;
		}
		painter.setPen(selected && hasFocus() ? normal : opcodeColor);
		painter.drawText(column * cw - shift, baseline, instruction.text.left(3));
		painter.setPen(selected && hasFocus() ? normal : operandColor);
		painter.drawText((column + 3) * cw - shift, baseline, instruction.text.mid(3));
		if (instruction.info != nullptr && !instruction.info->official)
		{
			painter.setPen(normal);
			painter.drawText((column + instruction.text.size() + 1) * cw - shift, baseline, QStringLiteral("*"));
		}
		painter.restore();
	}
}

void DisassemblyView::resizeEvent(QResizeEvent *event)
{
	QAbstractScrollArea::resizeEvent(event);
	rebuildLines();
}

void DisassemblyView::changeEvent(QEvent *event)
{
	QAbstractScrollArea::changeEvent(event);
	if (event->type() == QEvent::FontChange)
	{
		horizontalScrollBar()->setSingleStep(charWidth());
		rebuildLines();
	}
}

void DisassemblyView::scrollLines(int count)
{
	int address = topAddress();
	for (int i = 0; i < std::abs(count); ++i)
	{
		address = count > 0 ? nextAddress(address) : previousAddress(address);
	}
	verticalScrollBar()->setValue(address);
}

void DisassemblyView::keyPressEvent(QKeyEvent *event)
{
	if (event->matches(QKeySequence::Copy))
	{
		copySelection();
	}
	else
	{
		switch (event->key())
		{
			case Qt::Key_Up: scrollLines(-1); break;
			case Qt::Key_Down: scrollLines(1); break;
			case Qt::Key_PageUp: scrollLines(-std::max(1, int(lines.size()) * 3 / 4)); break;
			case Qt::Key_PageDown: scrollLines(std::max(1, int(lines.size()) * 3 / 4)); break;
			case Qt::Key_Home: verticalScrollBar()->setValue(0); break;
			case Qt::Key_End: verticalScrollBar()->setValue(0xFFF0); break;
			case Qt::Key_B: if (available) emit breakpointRequested(operandTarget < 0 ? selection : uint(operandTarget)); break;
			case Qt::Key_M: if (available) emit bookmarkRequested(operandTarget < 0 ? selection : uint(operandTarget)); break;
			case Qt::Key_Return:
			case Qt::Key_Enter: if (operandTarget >= 0) goToAddress(uint16_t(operandTarget)); break;
			default: QAbstractScrollArea::keyPressEvent(event); return;
		}
	}
	event->accept();
}

bool DisassemblyView::selectAt(const QPoint &position)
{
	const int row = position.y() / lineHeight();
	if (position.y() < 0 || row >= lines.size())
	{
		return false;
	}
	const auto &instruction = lines[row];
	selection = instruction.address;
	const int character = (position.x() + horizontalScrollBar()->value()) / charWidth() - instructionColumn();
	operandTarget = character >= 4 && character < instruction.text.size() ? instruction.operandAddress : -1;
	viewport()->update();
	return true;
}

void DisassemblyView::mousePressEvent(QMouseEvent *event)
{
	setFocus(Qt::MouseFocusReason);
	const int row = int(event->position().y()) / lineHeight();
	if (event->button() == Qt::RightButton && row >= 0 && row < lines.size() &&
		lines[row].address >= std::min(selection, selectionAnchor) &&
		lines[row].address <= std::max(selection, selectionAnchor))
	{
		return;
	}
	if (!selectAt(event->position().toPoint()))
	{
		return;
	}
	selectionAnchor = selection;
	if (available && event->button() == Qt::LeftButton && event->position().x() < 2 * charWidth())
	{
		emit breakpointRequested(selection);
	}
	event->accept();
}

void DisassemblyView::mouseMoveEvent(QMouseEvent *event)
{
	if (event->buttons() & Qt::LeftButton)
	{
		selectAt(event->position().toPoint());
	}
	else
	{
		const int row = int(event->position().y()) / lineHeight();
		const int character = (int(event->position().x()) + horizontalScrollBar()->value()) / charWidth() - instructionColumn();
		const bool operand = row >= 0 && row < lines.size() && lines[row].operandAddress >= 0 &&
			character >= 4 && character < lines[row].text.size();
		viewport()->setCursor(operand ? Qt::PointingHandCursor : Qt::IBeamCursor);
		if (row >= 0 && row < lines.size())
		{
			const uint16_t address = lines[row].address;
			const long offset = snapshot.prgOffsets[address];
			viewport()->setToolTip(tr("CPU $%1\nMapped PRG offset: %2")
				.arg(hex(address, 4), offset < 0 ? tr("unavailable") : QLatin1Char('$') + hex(uint(offset), 6)));
		}
	}
}

void DisassemblyView::mouseDoubleClickEvent(QMouseEvent *event)
{
	if (!selectAt(event->position().toPoint()))
	{
		return;
	}
	if (event->button() == Qt::LeftButton && operandTarget >= 0)
	{
		goToAddress(uint16_t(operandTarget));
	}
}

void DisassemblyView::wheelEvent(QWheelEvent *event)
{
	wheelRemainder += event->angleDelta().y();
	const int steps = wheelRemainder / 120;
	wheelRemainder %= 120;
	if (steps != 0)
	{
		scrollLines(-steps * QApplication::wheelScrollLines());
	}
	else if (!event->pixelDelta().isNull())
	{
		scrollLines(-event->pixelDelta().y() / lineHeight());
	}
	event->accept();
}

void DisassemblyView::contextMenuEvent(QContextMenuEvent *event)
{
	if (!available)
	{
		return;
	}
	if (event->reason() == QContextMenuEvent::Mouse)
	{
		const int row = event->pos().y() / lineHeight();
		if (row < 0 || row >= lines.size())
		{
			return;
		}
		if (lines[row].address < std::min(selection, selectionAnchor) ||
			lines[row].address > std::max(selection, selectionAnchor))
		{
			selectAt(event->pos());
			selectionAnchor = selection;
		}
	}
	const uint address = operandTarget < 0 ? selection : uint(operandTarget);
	QMenu menu(this);
	menu.addAction(tr("Run to &Cursor"), this, [this]() { emit runToRequested(selection); });
	if (operandTarget >= 0)
	{
		menu.addAction(tr("Go to $%1").arg(hex(address, 4)), this, [this, address]() { goToAddress(uint16_t(address)); });
	}
	menu.addAction(tr("Toggle &Breakpoint"), this, [this, address]() { emit breakpointRequested(address); });
	menu.addAction(tr("Add Book&mark"), this, [this, address]() { emit bookmarkRequested(address); });
	menu.addSeparator();
	menu.addAction(tr("&Copy"), this, &DisassemblyView::copySelection);
	menu.exec(event->globalPos());
}
