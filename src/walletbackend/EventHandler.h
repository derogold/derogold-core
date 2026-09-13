// Copyright (c) 2018-2021, The DeroGold Developers
// Copyright (c) 2018-2019, The TurtleCoin Developers
//
// Please see the included LICENSE file for more information.

#pragma once

#include <functional>
#include <mutex>

/* A subscriber runs on the thread that fired the event and must not block:
   these are fired from block processing, so anything slow in a handler slows
   syncing down. Queue the work and return, which is what the one subscriber in
   this codebase does.

   It used to launch a detached std::thread per event instead. That put no
   bound on anything - a rescan finding ten thousand transactions launched ten
   thousand threads - and a detached thread outliving the wallet it captured
   was a use after free waiting to happen, since nothing joined them at
   shutdown. */
template<typename T> class Event
{
  public:
    void subscribe(std::function<void(T)> function)
    {
        std::scoped_lock lock(m_mutex);

        m_function = function;
    }

    void unsubscribe()
    {
        std::scoped_lock lock(m_mutex);

        m_function = {};
    }

    void pause()
    {
        std::scoped_lock lock(m_mutex);

        m_paused = true;
    }

    void resume()
    {
        std::scoped_lock lock(m_mutex);

        m_paused = false;
    }

    void fire(T args)
    {
        /* Held across the call, so unsubscribing cannot free the function
           while it is running, and so a pause takes effect against events
           already in flight rather than racing them. A handler must therefore
           not subscribe, unsubscribe or pause from inside itself. */
        std::scoped_lock lock(m_mutex);

        /* If we have a function to run, and we're not ignoring events */
        if (m_function && !m_paused)
        {
            m_function(args);
        }
    }

  private:
    std::function<void(T)> m_function;

    bool m_paused = false;

    /* Guards both of the above. They are written from the wallet's own threads
       - a reset pauses events while the sync thread may be firing one - and
       were previously read and written with no synchronisation at all. */
    mutable std::mutex m_mutex;
};

class EventHandler
{
  public:
    Event<uint64_t> onSynced;

    Event<WalletTypes::Transaction> onTransaction;
};
