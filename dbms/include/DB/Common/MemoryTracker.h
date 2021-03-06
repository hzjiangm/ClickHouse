#pragma once

#include <atomic>
#include <common/Common.h>
#include <DB/Common/CurrentMetrics.h>


namespace CurrentMetrics
{
	extern const Metric MemoryTracking;
}


/** Отслеживает потребление памяти.
  * Кидает исключение, если оно стало бы больше некоторого предельного значения.
  * Один объект может использоваться одновременно в разных потоках.
  */
class MemoryTracker
{
	std::atomic<Int64> amount {0};
	std::atomic<Int64> peak {0};
	Int64 limit {0};

	/// В целях тестирования exception safety - кидать исключение при каждом выделении памяти с указанной вероятностью.
	double fault_probability = 0;

	/// Односвязный список. Вся информация будет передаваться в следующие MemoryTracker-ы тоже. Они должны жить во время жизни данного MemoryTracker.
	MemoryTracker * next = nullptr;

	/// You could specify custom metric to track memory usage.
	CurrentMetrics::Metric metric = CurrentMetrics::MemoryTracking;

	/// Если задано (например, "for user") - в сообщениях в логе будет указываться это описание.
	const char * description = nullptr;

public:
	MemoryTracker() {}
	MemoryTracker(Int64 limit_) : limit(limit_) {}

	~MemoryTracker();

	/** Вызывайте эти функции перед соответствующими операциями с памятью.
	  */
	void alloc(Int64 size);

	void realloc(Int64 old_size, Int64 new_size)
	{
		alloc(new_size - old_size);
	}

	/** А эту функцию имеет смысл вызывать после освобождения памяти.
	  */
	void free(Int64 size);

	Int64 get() const
	{
		return amount.load(std::memory_order_relaxed);
	}

	Int64 getPeak() const
	{
		return peak.load(std::memory_order_relaxed);
	}

	void setLimit(Int64 limit_)
	{
		limit = limit_;
	}

	void setFaultProbability(double value)
	{
		fault_probability = value;
	}

	void setNext(MemoryTracker * elem)
	{
		next = elem;
	}

	void setMetric(CurrentMetrics::Metric metric_)
	{
		metric = metric_;
	}

	void setDescription(const char * description_)
	{
		description = description_;
	}

	/// Обнулить накопленные данные.
	void reset();

	/// Вывести в лог информацию о пиковом потреблении памяти.
	void logPeakMemoryUsage() const;
};


/** Объект MemoryTracker довольно трудно протащить во все места, где выделяются существенные объёмы памяти.
  * Поэтому, используется thread-local указатель на используемый MemoryTracker или nullptr, если его не нужно использовать.
  * Этот указатель выставляется, когда в данном потоке следует отслеживать потребление памяти.
  * Таким образом, его нужно всего-лишь протащить во все потоки, в которых обрабатывается один запрос.
  */
extern __thread MemoryTracker * current_memory_tracker;


#include <boost/noncopyable.hpp>

struct TemporarilyDisableMemoryTracker : private boost::noncopyable
{
	MemoryTracker * memory_tracker;

	TemporarilyDisableMemoryTracker()
	{
		memory_tracker = current_memory_tracker;
		current_memory_tracker = nullptr;
	}

	~TemporarilyDisableMemoryTracker()
	{
		current_memory_tracker = memory_tracker;
	}
};
