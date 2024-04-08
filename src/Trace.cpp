#include "global.h"
#include "Trace.h"

#include <limits>
#include <iomanip>
#include <string>
#include <sstream>

Trace *TRACE = nullptr;

struct csvQuote
{
	csvQuote(const std::string& text)
		: text(text)
	{
	}

	const std::string& text;
};

std::ostream& operator<<(std::ostream& stream, const csvQuote& quote)
{
	const char* text = quote.text.c_str();
	if (!*text)
		return stream;

	stream << '"';

	do
	{
		if (*text == '"')
			stream << "\"\"";
		else
			stream << *text;

	} while (*++text);

	return stream << '"';
}

Trace::Trace()
{
	ASSERT(frameDump.Open("/Trace/trace_frame.csv", RageFile::WRITE));
	ASSERT(updateSongPositionDump.Open("/Trace/trace_songposition.csv", RageFile::WRITE));
	ASSERT(judgmentDump.Open("/Trace/trace_judgment.csv", RageFile::WRITE));

	frameDump.Write("frameNumber"
		",beginFrameTimestamp"
		",beforeUpdateTimestamp"
		",deltaTimeNoRate"
		",deltaTime"
		",updateRate"
		",beforeDevicesChangedTimestamp"
		",beforeDrawTimestamp"
		",endFrameTimestamp"
		"\n");

	updateSongPositionDump.Write("frameNumber"
		",timestamp"
		",cause"
		",isPlaying"
		",currentHardwareFrame"
		",stoppedSourceFrame"
		",samplerate"
		",approximate"
		",sourceFrame"
		",origTime"
		",origTimeAdjust"
		",origRageTimer"
		",origRageTimerAgo"
		",songPath"
		",actualTime"
		",actualRageTimer"
		",actualRageTimerAgo"
		",newPositionThisFrame"
		",estimatedTimeSinceLastPosition"
		"\n");

	judgmentDump.Write("frameNumber"
		",timestamp"
		",player"
		",col"
		",row"
		",tm"
		",held"
		",release"
		",positionMusicSeconds"
		",lastBeatUpdate"
		",lastBeatUpdateAgo"
		",songBeat"
		",positionSeconds"
		",timeSinceStep"
		",foundRow"
		",stepBeat"
		",stepSeconds"
		",currentMusicSeconds"
		",musicSeconds"
		",noteOffset"
		"\n");
}

Trace::~Trace()
{
	frameDump.Close();
	updateSongPositionDump.Close();
	judgmentDump.Close();
}

void Trace::EndFrame()
{
	std::ostringstream csvLine;
	std::uint64_t endFrameTimestamp = micros();
	csvLine << std::setprecision(std::numeric_limits<float>::max_digits10) << std::scientific
		<< frameNumber
		<< ',' << beginFrameTimestamp
		<< ',' << beforeUpdateTimestamp
		<< ',' << deltaTimeNoRate
		<< ',' << deltaTime
		<< ',' << updateRate
		<< ',' << beforeDevicesChangedTimestamp
		<< ',' << beforeDrawTimestamp
		<< ',' << endFrameTimestamp
		<< '\n';
	frameDump.Write(csvLine.str());
}

void Trace::EndUpdateSongPosition()
{
	std::ostringstream csvLine;
	csvLine << std::setprecision(std::numeric_limits<float>::max_digits10) << std::scientific
		<< frameNumber
		<< ',' << USPTimestamp
		<< ',' << csvQuote(USPCause)
		<< ',' << USPIsPlaying
		<< ',' << USPCurrentHardwareFrame
		<< ',' << USPStoppedSourceFrame
		<< ',' << USPSamplerate
		<< ',' << USPApproximate
		<< ',' << USPSourceFrame
		<< ',' << USPOrigTime
		<< ',' << USPOrigAdjust
		<< ',' << USPOrigTm
		<< ',' << USPOrigTmAgo
		<< ',' << csvQuote(USPSong)
		<< ',' << USPActualTime
		<< ',' << USPActualTm
		<< ',' << USPActualTmAgo
		<< ',' << USPNewPositionThisFrame
		<< ',' << USPEstimatedTimeSinceLastPosition
		<< '\n';
	updateSongPositionDump.Write(csvLine.str());

	USPCause = "UNKNOWN";
	USPOrigTime = 0;
	USPOrigAdjust = 0;
	USPOrigTm = 0;
	USPOrigTmAgo = 0;
	USPSong = "UNKNOWN";
}

void Trace::EndJudgment()
{
	std::ostringstream csvLine;
	csvLine << std::setprecision(std::numeric_limits<float>::max_digits10) << std::scientific
		<< frameNumber
		<< ',' << JudgmentTimestamp
		<< ',' << JudgmentPlayer
		<< ',' << JudgmentCol
		<< ',' << JudgmentRow
		<< ',' << JudgmentTm
		<< ',' << JudgmentHeld
		<< ',' << JudgmentRelease
		<< ',' << JudgmentPositionMusicSeconds
		<< ',' << JudgmentLastBeatUpdate
		<< ',' << JudgmentLastBeatUpdateAgo
		<< ',' << JudgmentSongBeat
		<< ',' << JudgmentPositionSeconds
		<< ',' << JudgmentTimeSinceStep
		<< ',' << JudgmentFoundRow
		<< ',' << JudgmentStepBeat
		<< ',' << JudgmentStepSeconds
		<< ',' << JudgmentCurrentMusicSeconds
		<< ',' << JudgmentMusicSeconds
		<< ',' << JudgmentNoteOffset
		<< '\n';
	judgmentDump.Write(csvLine.str());

	JudgmentStepBeat = 0;
	JudgmentStepSeconds = 0;
	JudgmentCurrentMusicSeconds = 0;
	JudgmentMusicSeconds = 0;
	JudgmentNoteOffset = 0;
}
