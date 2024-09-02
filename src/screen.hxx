// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright The Music Player Daemon Project

#ifndef SCREEN_H
#define SCREEN_H

#include "config.h"
#include "TitleBar.hxx"
#include "ProgressBar.hxx"
#include "StatusBar.hxx"
#include "History.hxx"
#include "ui/Point.hxx"
#include "ui/Window.hxx"
#include "event/IdleEvent.hxx"

#include <curses.h>

#include <map>
#include <memory>
#include <string>
#include <utility>

enum class Command : unsigned;
struct mpd_song;
struct mpdclient;
struct PageMeta;
class Page;
class DelayedSeek;
class EventLoop;

class ScreenManager {
	/**
	 * This event defers Paint() calls until after the EventLoop
	 * has handled all other events.
	 */
	IdleEvent paint_event;

	struct Layout {
		Size size;

		static constexpr int title_y = 0, title_x = 0;
		static constexpr int main_y = TitleBar::GetHeight(), main_x = 0;
		static constexpr int progress_x = 0;
		static constexpr int status_x = 0;

		constexpr explicit Layout(Size _size)
			:size(_size) {}

		constexpr unsigned GetMainRows() const {
			return GetProgressY() - main_y;
		}

		constexpr Size GetMainSize() const {
			return {size.width, GetMainRows()};
		}

		constexpr int GetProgressY() const {
			return GetStatusY() - 1;
		}

		constexpr int GetStatusY() const {
			return size.height - 1;
		}
	};

	Layout layout;

	TitleBar title_bar;
public:
	UniqueWindow main_window;

private:
	ProgressBar progress_bar;

public:
	StatusBar status_bar;

private:
	using PageMap = std::map<const PageMeta *,
				 std::unique_ptr<Page>>;
	PageMap pages;
	PageMap::iterator current_page = pages.begin();

	const PageMeta *mode_fn_prev;

	char *buf;
	size_t buf_size;

public:
	std::string findbuf;
	History find_history;

	explicit ScreenManager(EventLoop &_event_loop) noexcept;
	~ScreenManager() noexcept;

	auto &GetEventLoop() const noexcept {
		return paint_event.GetEventLoop();
	}

	void Init(struct mpdclient *c) noexcept;
	void Exit() noexcept;

	Point GetMainPosition() const noexcept {
		return {0, (int)title_bar.GetHeight()};
	}

	const PageMeta &GetCurrentPageMeta() const noexcept {
		return *current_page->first;
	}

	PageMap::iterator MakePage(const PageMeta &sf) noexcept;

	void OnResize() noexcept;

	[[gnu::pure]]
	bool IsVisible(const Page &page) const noexcept {
		return &page == current_page->second.get();
	}

	void Switch(const PageMeta &sf, struct mpdclient &c) noexcept;
	void Swap(struct mpdclient &c, const struct mpd_song *song) noexcept;

	void PaintTopWindow() noexcept;

	void Update(struct mpdclient &c, const DelayedSeek &seek) noexcept;
	void OnCommand(struct mpdclient &c, DelayedSeek &seek, Command cmd);

#ifdef HAVE_GETMOUSE
	bool OnMouse(struct mpdclient &c, DelayedSeek &seek,
		     Point p, mmask_t bstate);
#endif

	void SchedulePaint() noexcept {
		paint_event.Schedule();
	}

private:
	void NextMode(struct mpdclient &c, int offset) noexcept;

	void Paint() noexcept;
};

#endif
