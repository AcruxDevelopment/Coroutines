#include <iostream>
#include <unordered_map>
#include <cassert>
#include <acrux/coroutines/Coroutine.h>
#include <acrux/coroutines/CoroutineRunner.h>
#include <acrux/coroutines/standard/WaitConditions.h>
using namespace acrux::coroutines;
using namespace acrux::coroutines::standard;

#define yield co_yield
#define exit co_return

struct AlphaController
{
	float alpha = 1;

	Coroutine fadeIn(float delta)
	{
		std::cout << "alpha: " << alpha << '\n';
		while(alpha < 1)
		{
			alpha += delta;
			if(alpha > 1) alpha = 1;
			std::cout << "alpha: " << alpha << '\n';
			yield waitForSeconds(0.15);
		}
	}

	Coroutine fadeOut(float delta)
	{
		std::cout << "alpha: " << alpha << '\n';
		while(alpha > 0)
		{
			alpha -= delta;
			if(alpha < 0) alpha = 0;
			std::cout << "alpha: " << alpha << '\n';
			yield waitForSeconds(0.15);
		}
	}	
};

Coroutine blink(AlphaController& controller, float delta, float waitSeconds = 1)
{
	yield controller.fadeOut(delta);
	yield waitForSeconds(waitSeconds);
	yield controller.fadeIn(delta);
}

Coroutine typewrite(const std::string& text, float delay = 0.03)
{
	for(char c : text)
	{
		std::cout << c;
		std::cout.flush();
		yield waitForSeconds(delay);
	}
}

Coroutine writeAndWait(const std::string& text)
{
	yield typewrite(text);
	std::cin.get();
}

Coroutine rudeBuster()
{
	yield waitForSeconds(2);
	std::cout << "(RudeBuster Hit)\n";
}

Coroutine attack()
{
	std::cout << "(Attacking)\n";
	yield waitForSeconds(2);

	std::srand(std::time(nullptr)); 
	bool cancel = (std::rand() % 100) < 50;
	if(cancel)
	{
		std::cout << "(Miss)\n";
		exit;
	}

	std::cout << "(Hit)\n";
}

Coroutine myCutscene()
{
	yield writeAndWait("* Susie used RUDE BUSTER!");
	//yield rudeBuster();
	//yield waitForSeconds(1);
	CoroutineHandle buster = startCoroutine(rudeBuster());
	yield writeAndWait("* Ralsei cast HEAL PLAYER.");
	//yield waitForSeconds(1);
	buster.stop();
	yield attack();
	//stopCoroutine(buster);
	//yield buster;
	yield writeAndWait("* You won!\n* Got 0 EXP and 249 D$.");
}

// ---------------------------------------------------------------------------
// Demonstrates deriving from CoroutineRunner to attach extra data to each
// coroutine it starts, keyed by CoroutineHandle -- e.g. "which entity does
// this coroutine belong to?" in a game engine. CoroutineRunner itself knows
// nothing about this; it only provides the extension points
// (onCoroutineStarted/onCoroutineFinished) and a hashable CoroutineHandle.
// ---------------------------------------------------------------------------

using EntityId = int;

class GameCoroutineRunner : public CoroutineRunner
{
public:
	// Without this, declaring our own startCoroutine(task, owner) below would
	// hide CoroutineRunner::startCoroutine(task) entirely -- C++ overload
	// resolution doesn't look at base class overloads once a derived class
	// declares any overload with the same name.
	using CoroutineRunner::startCoroutine;

	// A new overload alongside the base class's plain startCoroutine(task),
	// which remains available and untouched.
	CoroutineHandle startCoroutine(Coroutine task, EntityId owner)
	{
		CoroutineHandle handle = CoroutineRunner::startCoroutine(std::move(task));
		if (handle) m_owners[handle] = owner;
		return handle;
	}

	EntityId ownerOf(CoroutineHandle handle) const
	{
		auto it = m_owners.find(handle);
		return it != m_owners.end() ? it->second : -1;
	}

	size_t trackedCount() const { return m_owners.size(); }

protected:
	// If this coroutine was started by another one we're tracking an owner
	// for -- whether via `co_yield childTask()` or a direct startCoroutine()
	// call made from inside that coroutine's body -- inherit that owner
	// automatically. The explicit-owner startCoroutine(task, owner) overload
	// above still runs its own m_owners[handle] = owner afterward, so an
	// explicit owner always wins over an inherited one.
	void onCoroutineStarted(CoroutineHandle handle, CoroutineHandle parent) override
	{
		auto it = m_owners.find(parent);
		if (it != m_owners.end())
		{
			m_owners[handle] = it->second;
		}
	}

	void onCoroutineFinished(CoroutineHandle handle) override
	{
		m_owners.erase(handle);
	}

private:
	std::unordered_map<CoroutineHandle, EntityId> m_owners;
};

Coroutine countToThree()
{
	co_yield nullptr;
	co_yield nullptr;
	co_yield nullptr;
}

// Starts a child coroutine two different ways -- co_yield, and a direct
// startCoroutine() call -- both of which should hand the child the same
// owner as this coroutine, with no explicit owner passed at either call site.
CoroutineHandle g_directChildHandle;

Coroutine parentTask()
{
	co_yield countToThree(); // child #1: via co_yield, waits for it to finish
	g_directChildHandle = startCoroutine(countToThree()); // child #2: fire-and-forget
	co_yield nullptr;
}

void testExtensionMechanism()
{
	GameCoroutineRunner runner;

	CoroutineHandle a = runner.startCoroutine(countToThree(), /* owner */ 42);
	CoroutineHandle b = runner.startCoroutine(countToThree()); // base overload, no owner

	assert(runner.ownerOf(a) == 42);
	assert(runner.ownerOf(b) == -1); // never registered
	assert(runner.trackedCount() == 1);

	while (a.isRunning() || b.isRunning())
	{
		runner.update();
	}

	// Both coroutines have finished; onCoroutineFinished should have
	// cleaned up the side table automatically, with no leftover entries.
	assert(runner.trackedCount() == 0);

	// A coroutine started with an owner. Its children -- one started via
	// co_yield, one via a direct startCoroutine() call from inside its
	// body -- should both inherit that same owner automatically, without
	// either call site ever passing it explicitly.
	CoroutineHandle parent = runner.startCoroutine(parentTask(), /* owner */ 7);
	assert(runner.trackedCount() == 1); // just `parent` so far

	runner.update(); // runs parentTask up to `co_yield countToThree()`,
	                  // which starts child #1 on this same tick

	// The only way trackedCount can have grown is if onCoroutineStarted
	// found `parent` in the owner table and propagated it to child #1.
	assert(runner.trackedCount() == 2);

	while (parent.isRunning())
	{
		runner.update();
	}

	// child #2 (started via a direct startCoroutine() call in parentTask's
	// body, after child #1 finished) should have inherited the same owner.
	// parentTask doesn't wait on child #2, so it's still running/tracked here.
	assert(runner.ownerOf(g_directChildHandle) == 7);

	// Drain everything so the next test starts from a clean runner.
	while (g_directChildHandle.isRunning())
	{
		runner.update();
	}
	assert(runner.trackedCount() == 0);

	std::cout << "[PASS] testExtensionMechanism\n";
}

int main()
{
	testExtensionMechanism();

	CoroutineRunner engine;
	AlphaController alphaController;

	engine.startCoroutine(myCutscene());

    while(engine.getActiveCount() > 0)
	{
        engine.update();
    }
    return 0;
}

