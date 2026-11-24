#include "audio/AudioDriverQt.h"

#include <cstring>
#include <functional>

#include <QAudioDevice>
#include <QAudioFormat>
#include <QAudioSink>
#include <QIODevice>
#include <QMediaDevices>
#include <QMetaObject>
#include <QObject>
#include <QThread>
#include <QtDebug>

namespace
{
	// Pull mode source of the sink. readData() runs on the audio thread
	// whenever the sink wants more samples.
	class PullDevice : public QIODevice
	{
		public:
			using FillFunction = std::function<void(int16_t *, int)>;

			PullDevice(FillFunction fillFunction, int channelCount)
				: fill(std::move(fillFunction)), channels(channelCount)
			{
			}

			bool isSequential(void) const override { return true; }

			// Always report data, silence is generated when the FIFO is empty
			qint64 bytesAvailable(void) const override { return 1 << 20; }

		protected:
			qint64 readData(char *data, qint64 maxlen) override
			{
				const int bytesPerFrame = int(sizeof(int16_t)) * channels;
				const int frames = int(maxlen / bytesPerFrame);

				if (frames <= 0)
				{
					return 0;
				}

				scratch.resize(size_t(frames));
				fill(scratch.data(), frames);

				if (channels == 1)
				{
					std::memcpy(data, scratch.data(), size_t(frames) * sizeof(int16_t));
				}
				else
				{
					int16_t *out = reinterpret_cast<int16_t *>(data);

					for (int i = 0; i < frames; ++i)
					{
						for (int c = 0; c < channels; ++c)
						{
							*out++ = scratch[size_t(i)];
						}
					}
				}

				return qint64(frames) * bytesPerFrame;
			}

			qint64 writeData(const char *, qint64) override { return -1; }

		private:
			FillFunction fill;
			int channels;
			std::vector<int16_t> scratch;
	};
}

// Lives on the audio thread, owns the sink and its source
class QtAudioContext : public QObject
{
	public:
		PullDevice *source = nullptr;
		QAudioSink *sink = nullptr;
		QString description;
};

AudioDriverQt::AudioDriverQt(void)
	: thread(nullptr),
	  context(nullptr)
{
}

AudioDriverQt::~AudioDriverQt(void)
{
	kill();
}

QString AudioDriverQt::driverInfo(void) const
{
	return QStringLiteral("Qt %1 (%2)")
		.arg(QString::fromLatin1(qVersion()))
		.arg(context != nullptr ? context->description : QStringLiteral("closed"));
}

bool AudioDriverQt::openDevice(unsigned int sampleRate, const QString &deviceId)
{
	QAudioDevice device = QMediaDevices::defaultAudioOutput();

	if (!deviceId.isEmpty())
	{
		for (const QAudioDevice &candidate : QMediaDevices::audioOutputs())
		{
			if (QString::fromUtf8(candidate.id()) == deviceId || candidate.description() == deviceId)
			{
				device = candidate;
				break;
			}
		}
	}

	if (device.isNull())
	{
		qWarning() << "Qt audio: no output device available";
		return false;
	}

	// The emulator produces mono. Fall back to duplicated stereo if the
	// device refuses it.
	QAudioFormat format;
	format.setSampleRate(int(sampleRate));
	format.setChannelCount(1);
	format.setSampleFormat(QAudioFormat::Int16);

	if (!device.isFormatSupported(format))
	{
		format.setChannelCount(2);
	}

	const int channels = format.channelCount();
	// A small device side buffer, the FIFO of the base class sets the latency
	const int sinkBufferBytes = periodSamples() * 2 * channels * int(sizeof(int16_t));

	thread = new QThread();
	thread->setObjectName(QStringLiteral("NestopiaQtAudio"));
	thread->start();

	context = new QtAudioContext();
	context->description = device.description();
	context->moveToThread(thread);

	bool ok = false;
	QtAudioContext *ctx = context;

	QMetaObject::invokeMethod(ctx, [this, ctx, device, format, channels, sinkBufferBytes, &ok]()
	{
		ctx->source = new PullDevice([this](int16_t *out, int count) { fillAudio(out, count); }, channels);
		// Unbuffered, else QIODevice reads ahead more than the FIFO holds
		ctx->source->open(QIODevice::ReadOnly | QIODevice::Unbuffered);

		ctx->sink = new QAudioSink(device, format);
		ctx->sink->setBufferSize(sinkBufferBytes);
		ctx->sink->start(ctx->source);

		if (ctx->sink->error() != QAudio::NoError || ctx->sink->state() == QAudio::StoppedState)
		{
			qWarning() << "Qt audio: output failed to start, error" << int(ctx->sink->error());
			ctx->sink->stop();
			delete ctx->sink;
			ctx->sink = nullptr;
			delete ctx->source;
			ctx->source = nullptr;
			return;
		}

		ok = true;
	}, Qt::BlockingQueuedConnection);

	if (!ok)
	{
		closeDevice();
		return false;
	}

	return true;
}

void AudioDriverQt::closeDevice(void)
{
	if (thread == nullptr)
	{
		return;
	}

	QtAudioContext *ctx = context;

	QMetaObject::invokeMethod(ctx, [ctx]()
	{
		if (ctx->sink != nullptr)
		{
			ctx->sink->stop();
			delete ctx->sink;
			ctx->sink = nullptr;
		}

		delete ctx->source;
		ctx->source = nullptr;
	}, Qt::BlockingQueuedConnection);

	// The context is deleted by its own thread as the thread exits
	QObject::connect(thread, &QThread::finished, ctx, &QObject::deleteLater);
	thread->quit();
	thread->wait();

	delete thread;
	thread = nullptr;
	context = nullptr;
}
