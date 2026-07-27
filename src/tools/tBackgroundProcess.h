/*

*************************************************************************

ArmageTron -- Just another Tron Lightcycle Game in 3D.
Copyright (C) 2000  Manuel Moos (manuel@moosnet.de)

**************************************************************************

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
  
***************************************************************************

*/

#ifndef     TBACKGROUNDPROCESS_H_INCLUDED
#define     TBACKGROUNDPROCESS_H_INCLUDED

#include "tToDo.h"
#include "tThread.h"
#include "tLockedQueue.h"
#include "tSafePTR.h"
#include "tMemManager.h"

#include <utility>

#ifdef __3DS__
//! Limits how many detached background tasks run at once.
//!
//! The desktop client is happy to spawn one thread per pending task, and the
//! server browser turns a list of a hundred servers into a hundred pending DNS
//! lookups. On a 3DS every one of those threads takes its stack out of the
//! same heap the game runs in, and the homebrew launcher's grant varies: one
//! observed run had 33 MiB of heap rather than 95 MiB. Fifty threads was
//! enough to wedge the console partway through loading the list.
class tBackgroundLimit
{
public:
    static int const maximum = 3;

    //! Reserves a slot, returning false if they are all taken.
    static bool Acquire()
    {
        boost::unique_lock< boost::mutex > lock( Mutex() );
        if ( Count() >= maximum )
            return false;
        ++Count();
        return true;
    }

    static void Release()
    {
        boost::unique_lock< boost::mutex > lock( Mutex() );
        --Count();
    }
private:
    static int & Count(){ static int count = 0; return count; }
    static boost::mutex & Mutex(){ static boost::mutex mutex; return mutex; }
};
#endif

//! template that runs void member functions of reference countable objects
template< class T > class tMemberFunctionRunnerTemplate
{
private:
public:
    tMemberFunctionRunnerTemplate( T & object, void (T::*function)() )
    : object_( &object ), function_( function )
    {
    }

    //! runs the function
    void run()
    {
        (object_->*function_)();
    }

    //! runs the function, too. 
    void operator () ()
    {
        run();
    }

#ifdef __3DS__
    //! Frees the slot the task occupied once it finishes.
    struct Counted
    {
        tMemberFunctionRunnerTemplate<T> runner_;
        explicit Counted( tMemberFunctionRunnerTemplate<T> const & runner )
        : runner_( runner ) {}
        void operator () ()
        {
            runner_.run();
            tBackgroundLimit::Release();
        }
    };
#endif

    //! schedule a task for execution in a background thread
    static void ScheduleBackground( T & object, void (T::*function)()  )
    {
#ifdef __3DS__
        tMemberFunctionRunnerTemplate<T> runner( object, function );

        // All slots busy: do the work here rather than add another thread.
        // Slower than running it in the background, but it finishes.
        if ( !tBackgroundLimit::Acquire() )
        {
            runner.run();
            return;
        }

        try
        {
            boost::thread::attributes threadAttributes;
            // Enough for a name lookup and the strings around it. The desktop
            // eight megabytes, or even the quarter megabyte this used to ask
            // for, is a lot to take from a heap this size.
            threadAttributes.set_stack_size( 96 * 1024 );
            boost::thread( threadAttributes, Counted( runner ) ).detach();
        }
        catch ( ... )
        {
            // No thread to be had. Run it here instead of losing the task.
            tBackgroundLimit::Release();
            runner.run();
        }
        return;
#else
        // schedule the task into a background thread
#if BOOST_VERSION >= 105300
        // attributes are not available on all supporting platforms. The version check is inprecise,
        // that is simply the boost version that is in winlibs that has attributes available.
        boost::thread::attributes threadAttributes;
#ifdef __3DS__
        threadAttributes.set_stack_size(256 * 1024);
#else
        threadAttributes.set_stack_size(8388608);
#endif
        boost::thread(threadAttributes, tMemberFunctionRunnerTemplate<T>( object, function ) ).detach();
#else
        boost::thread(tMemberFunctionRunnerTemplate<T>( object, function ) ).detach();
#endif
#endif
    }

    //! schedule a task for execution in the next tToDo call
    static void ScheduleForeground( T & object, void (T::*function)()  )
    {
        Pending().add( tMemberFunctionRunnerTemplate( object, function ) );
        st_ToDoOnce( FinishAll );
    }
private:
    // queue of foreground tasks
    typedef tLockedQueue< tMemberFunctionRunnerTemplate, boost::mutex > Queue;
    static Queue & Pending()
    {
        static Queue pending;
        return pending;
    }

    // function that calls them
    static void FinishAll()
    {
        // finish all pending tasks
        while( Pending().size() > 0 )
        {
            tMemberFunctionRunnerTemplate next = Pending().next();
            next.run();
        }
    }

    //! pointer to the object we should so something with
    tJUST_CONTROLLED_PTR< T > object_;
    
    //! the function to call
    void (T::*function_)();
};

//! convenience wrapper
class tMemberFunctionRunner
{
public:
    //! runs a member function in a background thread
    template< class T > static void ScheduleBackground( T & object, void (T::*function)() )
    {
        tMemberFunctionRunnerTemplate<T>::ScheduleBackground( object, function );
    }

    //! runs a member function on the next call of st_DoToDo()
    template< class T > static void ScheduleForeground( T & object, void (T::*function)() )
    {
        tMemberFunctionRunnerTemplate<T>::ScheduleForeground( object, function );
    }
};

//! runs lambda functions or equivalents
class tLambdaRunner
{
    template <typename F>
    struct LambdaHolder : public tReferencable<LambdaHolder<F>>
    {
        LambdaHolder(LambdaHolder const&) = default;
        LambdaHolder(LambdaHolder&&) = default;
        LambdaHolder(F const& f) : f_{f} {};
        LambdaHolder(F&& f) : f_{std::move(f)} {};

        F f_;

        void run()
        {
            f_();
        }
    };

public:
    //! runs a member function in a background thread
    template <typename F>
    static void ScheduleBackground(F&& f)
    {
        using T = LambdaHolder<F>;
        auto* pHolder = tNEW(T)(std::move(f));
        tMemberFunctionRunnerTemplate<T>::ScheduleBackground(*pHolder, &T::run);
    }

    //! runs a member function on the next call of st_DoToDo()
    template <typename F>
    static void ScheduleForeground(F&& f)
    {
        using T = LambdaHolder<F>;
        auto* pHolder = tNEW(T)(std::move(f));
        tMemberFunctionRunnerTemplate<T>::ScheduleForeground(*pHolder, &T::run);
    }
};

#endif
