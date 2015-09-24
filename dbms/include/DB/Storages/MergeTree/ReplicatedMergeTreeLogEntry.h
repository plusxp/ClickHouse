#pragma once

#include <DB/Core/Exception.h>
#include <DB/Core/ErrorCodes.h>
#include <DB/Core/Types.h>
#include <DB/IO/WriteHelpers.h>

#include <mutex>
#include <condition_variable>


struct Stat;


namespace DB
{

class ReadBuffer;
class WriteBuffer;
class StorageReplicatedMergeTree;


/** Добавляет кусок в множество future_parts; в деструкторе убирает.
  * future_parts - множество кусков, которые будут созданы после выполнения
  *  выполняющихся в данный момент элементов очереди.
  */
struct FuturePartTagger
{
	String part;
	StorageReplicatedMergeTree & storage;

	FuturePartTagger(const String & part_, StorageReplicatedMergeTree & storage_);
	~FuturePartTagger();
};

typedef Poco::SharedPtr<FuturePartTagger> FuturePartTaggerPtr;


/// Запись о том, что нужно сделать. Только данные (их можно копировать).
struct ReplicatedMergeTreeLogEntryData
{
	enum Type
	{
		EMPTY,		 /// Не используется.
		GET_PART,    /// Получить кусок с другой реплики.
		MERGE_PARTS, /// Слить куски.
		DROP_RANGE,  /// Удалить куски в указанном месяце в указанном диапазоне номеров.
		ATTACH_PART, /// Перенести кусок из директории detached или unreplicated.
	};

	String typeToString() const
	{
		switch (type)
		{
			case ReplicatedMergeTreeLogEntryData::GET_PART: 	return "GET_PART";
			case ReplicatedMergeTreeLogEntryData::MERGE_PARTS: 	return "MERGE_PARTS";
			case ReplicatedMergeTreeLogEntryData::DROP_RANGE: 	return "DROP_RANGE";
			case ReplicatedMergeTreeLogEntryData::ATTACH_PART: 	return "ATTACH_PART";
			default:
				throw Exception("Unknown log entry type: " + DB::toString(type), ErrorCodes::LOGICAL_ERROR);
		}
	}

	String znode_name;

	Type type = EMPTY;
	String source_replica; /// Пустая строка значит, что эта запись была добавлена сразу в очередь, а не скопирована из лога.

	/// Имя куска, получающегося в результате.
	/// Для DROP_RANGE имя несуществующего куска. Нужно удалить все куски, покрытые им.
	String new_part_name;

	Strings parts_to_merge;

	/// Для DROP_RANGE, true значит, что куски нужно не удалить, а перенести в директорию detached.
	bool detach = false;

	/// Для ATTACH_PART имя куска в директории detached или unreplicated.
	String source_part_name;
	/// Нужно переносить из директории unreplicated, а не detached.
	bool attach_unreplicated = false;

	/// Доступ под queue_mutex.
	bool currently_executing = false;	/// Выполняется ли действие сейчас.
	/// Эти несколько полей имеют лишь информационный характер (для просмотра пользователем с помощью системных таблиц).
	/// Доступ под queue_mutex.
	size_t num_tries = 0;				/// Количество попыток выполнить действие (с момента старта сервера; включая выполняющееся).
	ExceptionPtr exception;				/// Последний эксепшен, в случае безуспешной попытки выполнить действие.
	time_t last_attempt_time = 0;		/// Время начала последней попытки выполнить действие.
	size_t num_postponed = 0;			/// Количество раз, когда действие было отложено.
	String postpone_reason;				/// Причина, по которой действие было отложено, если оно отложено.
	time_t last_postpone_time = 0;		/// Время последнего раза, когда действие было отложено.

	/// Время создания или время копирования из общего лога в очередь конкретной реплики.
	time_t create_time = 0;

	/// Величина кворума (для GET_PART) - ненулевое значение при включенной кворумной записи.
	size_t quorum = 0;
};


struct ReplicatedMergeTreeLogEntry : ReplicatedMergeTreeLogEntryData
{
	typedef Poco::SharedPtr<ReplicatedMergeTreeLogEntry> Ptr;

	FuturePartTaggerPtr future_part_tagger;
	std::condition_variable execution_complete; /// Пробуждается когда currently_executing становится false.

	void addResultToVirtualParts(StorageReplicatedMergeTree & storage);
	void tagPartAsFuture(StorageReplicatedMergeTree & storage);

	void writeText(WriteBuffer & out) const;
	void readText(ReadBuffer & in);

	String toString() const;
	static Ptr parse(const String & s, const Stat & stat);
};


}
