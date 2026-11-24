#pragma once

#include <array>

#include <QAbstractScrollArea>
#include <QMap>
#include <QVector>

#include "debugger/Disassembler.h"

class DisassemblyView : public QAbstractScrollArea
{
	Q_OBJECT

	public:
		struct Snapshot
		{
			std::array<int, 65536> bytes;
			std::array<long, 65536> prgOffsets;
		};

		explicit DisassemblyView(QWidget *parent = nullptr);
		void setSnapshot(const Snapshot &snapshot, uint16_t pc, bool followPC);
		void clear(void);
		void setBreakpoints(const QMap<uint, bool> &breakpoints);
		void goToAddress(uint16_t address);
		void goToPC(void);
		void navigateBack(void);
		void navigateForward(void);
		void setShowBytes(bool show);
		void setShowRomOffsets(bool show);
		void setPCPlacement(bool center);
		uint16_t selectedAddress(void) const { return selection; }
		uint16_t topAddress(void) const;
		QString selectedText(void) const;

	signals:
		void breakpointRequested(uint address);
		void bookmarkRequested(uint address);
		void runToRequested(uint address);

	protected:
		void paintEvent(QPaintEvent *event) override;
		void resizeEvent(QResizeEvent *event) override;
		void keyPressEvent(QKeyEvent *event) override;
		void mousePressEvent(QMouseEvent *event) override;
		void mouseDoubleClickEvent(QMouseEvent *event) override;
		void mouseMoveEvent(QMouseEvent *event) override;
		void wheelEvent(QWheelEvent *event) override;
		void contextMenuEvent(QContextMenuEvent *event) override;
		void changeEvent(QEvent *event) override;

	private:
		Disassembler::Instruction instructionAt(uint16_t address) const;
		int nextAddress(int address) const;
		int previousAddress(int address) const;
		void scrollLines(int count);
		void rebuildLines(void);
		bool selectAt(const QPoint &position);
		void copySelection(void) const;
		void reveal(uint16_t address);
		int lineHeight(void) const;
		int charWidth(void) const;
		int instructionColumn(void) const;
		QString lineText(const Disassembler::Instruction &instruction) const;

		Snapshot snapshot;
		QMap<uint, bool> breakpoints;
		QMap<int, int> predecessors;
		QVector<Disassembler::Instruction> lines;
		QVector<uint16_t> backHistory;
		QVector<uint16_t> forwardHistory;
		uint16_t pc = 0;
		uint16_t selection = 0;
		uint16_t selectionAnchor = 0;
		int operandTarget = -1;
		int wheelRemainder = 0;
		bool available = false;
		bool showBytes = true;
		bool showRomOffsets = false;
		bool centerPC = true;
};
