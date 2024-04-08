#ifndef TRACE_H
#define TRACE_H

#include <chrono>
#include <cstdint>
#include <string>

#include "RageFile.h"
#include "RageTimer.h"

class Trace
{
public:
	Trace();
	~Trace();

	static inline std::uint64_t micros()
	{
		return std::chrono::duration_cast<std::chrono::microseconds>(clock::now().time_since_epoch()).count();
	}

	static inline std::uint64_t timerMicros(const RageTimer& tm)
	{
		return static_cast<std::uint64_t>(tm.m_secs) * 1000000ull + static_cast<std::uint64_t>(tm.m_us);
	}

	inline void BeginFrame()
	{
		frameNumber++;
		beginFrameTimestamp = micros();
	}

	inline void BeforeUpdate()
	{
		beforeUpdateTimestamp = micros();
	}

	inline void SetDeltaTimeNoRate(float fDeltaTime)
	{
		deltaTimeNoRate = fDeltaTime;
	}

	inline void SetDeltaTime(float fDeltaTime, float fUpdateRate)
	{
		deltaTime = fDeltaTime;
		updateRate = fUpdateRate;
	}

	inline void BeforeDevicesChanged()
	{
		beforeDevicesChangedTimestamp = micros();
	}

	inline void BeforeDraw()
	{
		beforeDrawTimestamp = micros();
	}

	inline void UpdateSongPositionSetIsPlayingAndFramesAndSamplerate(bool bIsPlaying, std::int64_t iCurrentHardwareFrame, std::int64_t iStoppedSourceFrame, std::int64_t iSamplerate)
	{
		USPIsPlaying = bIsPlaying;
		USPCurrentHardwareFrame = iCurrentHardwareFrame;
		USPStoppedSourceFrame = iCurrentHardwareFrame;
		USPSamplerate = iSamplerate;
	}

	inline void UpdateSongPositionSetApproximate(bool bApproximate)
	{
		USPApproximate = bApproximate;
	}

	inline void UpdateSongPositionSetSourceFrame(std::int64_t iSourceFrame)
	{
		USPSourceFrame = iSourceFrame;
	}

	inline void UpdateSongPositionSetCause(const char* cause)
	{
		USPCause = cause;
	}

	inline void UpdateSongPositionSetTimeAdjust(float fUSPTime, float fUSPAdjust, const RageTimer& tm)
	{
		USPOrigTime = fUSPTime;
		USPOrigAdjust = fUSPAdjust;
		USPOrigTm = timerMicros(tm);
		USPOrigTmAgo = tm.Ago();
	}

	inline void UpdateSongPositionSetSong(const char* song)
	{
		USPSong = song;
	}

	inline void BeginUpdateSongPosition(float fPositionSeconds, const RageTimer& tm, bool bNewPositionThisFrame, float fEstimatedTimeSinceLastPosition)
	{
		USPTimestamp = micros();
		USPActualTime = fPositionSeconds;
		USPActualTm = timerMicros(tm);
		USPActualTmAgo = tm.Ago();
		USPNewPositionThisFrame = bNewPositionThisFrame;
		USPEstimatedTimeSinceLastPosition = fEstimatedTimeSinceLastPosition;
	}

	inline void BeginJudgment(int player, int col, int row, const RageTimer& tm, bool bHeld, bool bRelease, float fPositionMusicSeconds,
		const RageTimer& lastBeatUpdate, float lastBeatUpdateAgo, float songBeat, float positionSeconds, float timeSinceStep)
	{
		JudgmentPlayer = player;
		JudgmentTimestamp = micros();
		JudgmentCol = col;
		JudgmentRow = row;
		JudgmentTm = timerMicros(tm);
		JudgmentHeld = bHeld;
		JudgmentRelease = bRelease;
		JudgmentPositionMusicSeconds = fPositionMusicSeconds;
		JudgmentLastBeatUpdate = timerMicros(lastBeatUpdate);
		JudgmentLastBeatUpdateAgo = lastBeatUpdateAgo;
		JudgmentSongBeat = songBeat;
		JudgmentPositionSeconds = positionSeconds;
		JudgmentTimeSinceStep = timeSinceStep;
	}

	inline void JudgmentSetFoundRow(int foundRow)
	{
		JudgmentFoundRow = foundRow;
	}

	inline void JudgmentSetNoteData(float stepBeat, float stepSeconds, float currentMusicSeconds, float musicSeconds, float noteOffset)
	{
		JudgmentStepBeat = stepBeat;
		JudgmentStepSeconds = stepSeconds;
		JudgmentCurrentMusicSeconds = currentMusicSeconds;
		JudgmentMusicSeconds = musicSeconds;
		JudgmentNoteOffset = noteOffset;
	}

	void EndFrame();
	void EndUpdateSongPosition();
	void EndJudgment();

	std::uint64_t GetFrameNumber() const { return frameNumber; }
	std::uint64_t GetFrameTimestamp() const { return beginFrameTimestamp; }

private:
	using clock = std::conditional<
		std::chrono::high_resolution_clock::is_steady,
		std::chrono::high_resolution_clock,
		std::chrono::steady_clock>::type;

	std::uint64_t frameNumber = 0;
	std::uint64_t beginFrameTimestamp = 0;
	std::uint64_t beforeUpdateTimestamp = 0;
	float deltaTimeNoRate = 0;
	float deltaTime = 0;
	float updateRate = 0;
	std::uint64_t beforeDevicesChangedTimestamp = 0;
	std::uint64_t beforeDrawTimestamp = 0;

	bool USPIsPlaying = false;
	std::int64_t USPCurrentHardwareFrame = 0;
	std::int64_t USPStoppedSourceFrame = 0;
	std::int64_t USPSamplerate = 0;
	bool USPApproximate = false;
	std::int64_t USPSourceFrame = 0;
	std::string USPCause = "UNKNOWN";
	float USPOrigTime = 0;
	float USPOrigAdjust = 0;
	std::uint64_t USPOrigTm = 0;
	float USPOrigTmAgo = 0;
	std::string USPSong = "UNKNOWN";
	std::uint64_t USPTimestamp = 0;
	float USPActualTime = 0;
	std::uint64_t USPActualTm = 0;
	float USPActualTmAgo = 0;
	bool USPNewPositionThisFrame = false;
	float USPEstimatedTimeSinceLastPosition = 0;

	int JudgmentPlayer = 0;
	std::uint64_t JudgmentTimestamp = 0;
	int JudgmentCol = -1;
	int JudgmentRow = -1;
	std::uint64_t JudgmentTm = 0;
	bool JudgmentHeld = false;
	bool JudgmentRelease = false;
	float JudgmentPositionMusicSeconds = 0;
	std::uint64_t JudgmentLastBeatUpdate = 0;
	float JudgmentLastBeatUpdateAgo = 0;
	float JudgmentSongBeat = 0;
	float JudgmentPositionSeconds = 0;
	float JudgmentTimeSinceStep = 0;
	int JudgmentFoundRow = -1;
	float JudgmentStepBeat = 0;
	float JudgmentStepSeconds = 0;
	float JudgmentCurrentMusicSeconds = 0;
	float JudgmentMusicSeconds = 0;
	float JudgmentNoteOffset = 0;

	RageFile frameDump;
	RageFile updateSongPositionDump;
	RageFile judgmentDump;
};

extern Trace *TRACE;

#endif
