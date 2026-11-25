// GuiBusyProfiler.h
//
// Marks the time the GUI thread spends busy: from the event loop waking up until it
// is about to block again, rather than one marker per widget event. Install it as an
// event filter on the application object after the application was created.
//
// With NESTOPIAQT_EVENT_TALLY set in the environment, the number of events delivered to
// each receiver class by event type is printed to stderr every 5 seconds.

#pragma once

#ifdef NESTOPIAQT_PROFILER_ENABLE

#include <map>
#include <optional>
#include <string>
#include <utility>

#include <QElapsedTimer>
#include <QObject>

#include "profiler/Profiler.h"

class GuiBusyProfiler : public QObject
{
	public:
		explicit GuiBusyProfiler(QObject *parent = nullptr);

	protected:
		// Sees events (including timer events) before their handlers run, which can be
		// earlier than the dispatcher's awake signal.
		bool eventFilter(QObject *watched, QEvent *event) override;

	private:
		void beginBusy(void);
		void tallyEvent(QObject *watched, QEvent *event);

		std::optional<Profiler::FuncScoped> busy;

		bool tallyEnabled;
		QElapsedTimer tallyTimer;
		std::map<std::pair<std::string, int>, unsigned int> tally;
};

#endif // NESTOPIAQT_PROFILER_ENABLE
