#include "profiler/GuiBusyProfiler.h"

#ifdef NESTOPIAQT_PROFILER_ENABLE

#include <algorithm>
#include <cstdio>
#include <vector>

#include <QAbstractEventDispatcher>
#include <QEvent>
#include <QMetaObject>

GuiBusyProfiler::GuiBusyProfiler(QObject *parent)
	: QObject(parent),
	  tallyEnabled(qEnvironmentVariableIsSet("NESTOPIAQT_EVENT_TALLY"))
{
	QAbstractEventDispatcher *dispatcher = QAbstractEventDispatcher::instance();

	connect(dispatcher, &QAbstractEventDispatcher::awake, this, [this]() { beginBusy(); });
	connect(dispatcher, &QAbstractEventDispatcher::aboutToBlock, this, [this]() { busy.reset(); });
}

bool GuiBusyProfiler::eventFilter(QObject *watched, QEvent *event)
{
	beginBusy();

	if (tallyEnabled)
	{
		tallyEvent(watched, event);
	}

	return QObject::eventFilter(watched, event);
}

void GuiBusyProfiler::beginBusy(void)
{
	if (!busy.has_value())
	{
		static constexpr Profiler::Site site = { __LINE__, __FILE__, "GuiBusyProfiler", "GUI Event Loop Busy" };

		busy.emplace(&site);
	}
}

void GuiBusyProfiler::tallyEvent(QObject *watched, QEvent *event)
{
	if (!tallyTimer.isValid())
	{
		tallyTimer.start();
	}

	tally[std::make_pair(std::string(watched->metaObject()->className()), static_cast<int>(event->type()))]++;

	if (tallyTimer.elapsed() < 5000)
	{
		return;
	}

	std::vector<std::pair<unsigned int, std::pair<std::string, int>>> sorted;

	for (const auto &entry : tally)
	{
		sorted.push_back(std::make_pair(entry.second, entry.first));
	}

	std::sort(sorted.begin(), sorted.end(), [](const auto &a, const auto &b) { return a.first > b.first; });

	std::fprintf(stderr, "EVENT TALLY (%.1fs)  total=%zu\n", tallyTimer.elapsed() / 1000.0, tally.size());

	for (size_t i = 0; i < sorted.size() && i < 15; i++)
	{
		std::fprintf(stderr, "  %6u  %-28s type=%d\n", sorted[i].first, sorted[i].second.first.c_str(),
			sorted[i].second.second);
	}

	tally.clear();
	tallyTimer.restart();
}

#endif // NESTOPIAQT_PROFILER_ENABLE
