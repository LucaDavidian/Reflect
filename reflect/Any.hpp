#ifndef META_ANY_H
#define META_ANY_H

#include "TypeDescriptor.hpp"
#include <cstddef>
#include <type_traits>
#include <utility>
#include <string>
#include <exception>

namespace Reflect
{

	namespace Details
	{

		template <std::size_t SIZE, std::size_t ALIGNMENT = alignof(std::max_align_t)>
		struct AlignedStorage
		{
			static_assert(SIZE >= sizeof(void*), "storage must be at least the size of a pointer");

			struct Type
			{
				alignas(ALIGNMENT) unsigned char Storage[SIZE];
			};
		};

		template <std::size_t SIZE, std::size_t ALIGNMENT = alignof(std::max_align_t)>
		using AlignedStorageT = typename AlignedStorage<SIZE, ALIGNMENT>::Type;

	}  // namespace Details

	class BadCastException : public std::exception
	{
	public:
		BadCastException(const std::string &retrieved, const std::string &contained, const std::string &msg = "")
			: mMessage(msg + " wrong type from Get: tried to get " + retrieved + ", contained " + contained) {}

		const char *what() const noexcept override
		{
			return mMessage.c_str();
		}

	private:
		std::string mMessage;
	};

	template <std::size_t>
	class BasicAny;

	using Any = BasicAny<sizeof(void*)>;

	/*
	* AnyRef is an object that contains a pointer to any other object
	* but does not manage its lifetime
	*/
	class AnyRef
	{
		template <std::size_t> friend class BasicAny;

	public:
		AnyRef() : mInstance(nullptr), mType(nullptr) {}

		template <typename T, typename U = std::remove_cv_t<T>, typename = std::enable_if_t<!std::is_same_v<U, AnyRef>>>
		AnyRef(T &object) : mInstance(&object), mType(Details::Resolve<U>()) {}

		template <std::size_t SIZE>
		AnyRef(BasicAny<SIZE> &any) : mInstance(any.mInstance), mType(any.mType) {}

	private:
		void *mInstance;
		TypeDescriptor const *mType;
	};

	template <std::size_t SIZE>
	void swap(BasicAny<SIZE> &any1, BasicAny<SIZE> &any2)
	{
		any1.Swap(any2);
	}

	/*
	* Any acts as a container of an object of any kind, it either allocates the object dynamically 
	* on the heap or uses a SBO optimization for objects whose size is less than SIZE
	*/
	template <std::size_t SIZE>
	class BasicAny
	{
		friend class AnyRef;

	public:
		BasicAny();

		template <typename T, typename U = typename std::remove_cv<std::remove_reference_t<std::decay_t<T>>>::type, typename = typename std::enable_if<!std::is_same_v<U, BasicAny>>::type>
		BasicAny(T &&object);

		BasicAny(const BasicAny &other);
		BasicAny(BasicAny &&other);

		BasicAny(AnyRef handle);

		~BasicAny();

		template <typename T, typename U = typename std::remove_cv<std::remove_reference_t<std::decay_t<T>>>::type, typename = typename std::enable_if<!std::is_same<U, BasicAny>::value>::type>
		BasicAny &operator=(T &&object);

		BasicAny &operator=(const BasicAny &other);
		BasicAny &operator=(BasicAny &&other);

		void Swap(BasicAny &other);

		explicit operator bool() const { return Get() != nullptr; }

		const TypeDescriptor *GetType() const;

		const void *Get() const;
		
		void *Get();

		template <typename T>
		const T *TryCast() const;

		template <typename T>
		T *TryCast();

		template <typename T>
		BasicAny TryConvert() const;

		bool IsRef() const { return mCopy == nullptr; }  // check if it's a AnyRef

	private:
		void *mInstance;
		Details::AlignedStorageT<SIZE> mStorage;

		const TypeDescriptor *mType;

		typedef void *(*CopyFun)(void*, const void*);
		typedef void *(*MoveFun)(void*, void*);
		typedef void (*DestroyFun)(void*);

		CopyFun mCopy;
		MoveFun mMove;
		DestroyFun mDestroy;

		template <typename T, typename = std::void_t<> /* void */>
		struct TypeTraits
		{
			template <typename... Args>
			static void *New(void *storage, Args&&... args)
			{
				T *instance = new T(std::forward<Args>(args)...);
				new(storage) T*(instance);

				return instance;
			}

			static void *Copy(void *to, const void *from)
			{
				T *instance = new T(*static_cast<const T*>(from));
				new(to) T*(instance);

				return instance;
			}

			static void *Move(void *to, void *from)
			{
				T *instance = static_cast<T*>(from);
				new(to) T*(instance);

				return instance;
			}

			static void Destroy(void *instance)
			{
				delete static_cast<T*>(instance);
			}
		};

		template <typename T>
		struct TypeTraits<T, typename std::enable_if<sizeof(T) <= SIZE>::type>
		{
			template <typename... Args>
			static void *New(void *storage, Args&&... args)
			{
				new(storage) T(std::forward<Args>(args)...);

				return storage;
			}

			static void *Copy(void *to, const void *from)
			{
				new(to) T(*static_cast<const T*>(from));

				return to;
			}

			static void *Move(void *to, void *from)
			{
				T &instance = *static_cast<T*>(from);
				new(to) T(std::move(instance));
				instance.~T();

				return to;
			}

			static void Destroy(void *instance)
			{
				static_cast<T*>(instance)->~T();
			}
		};
	};

	template <std::size_t SIZE>
	BasicAny<SIZE>::BasicAny() : mInstance(nullptr), mCopy(nullptr), mMove(nullptr), mDestroy(nullptr), mType(nullptr)
	{
		new(&mStorage) std::nullptr_t(nullptr);
	}

	template <std::size_t SIZE>
	template <typename T, typename U, typename>
	BasicAny<SIZE>::BasicAny(T &&object) : mCopy(TypeTraits<U>::Copy), mMove(TypeTraits<U>::Move), mDestroy(TypeTraits<U>::Destroy), mType(Details::Resolve<U>())
	{
		mInstance = TypeTraits<U>::New(&mStorage, std::forward<T>(object));
	}

	template <std::size_t SIZE>
	BasicAny<SIZE>::BasicAny(const BasicAny &other) : mCopy(other.mCopy), mMove(other.mMove), mDestroy(other.mDestroy), mType(other.mType)
	{
		mInstance = other.mCopy ? other.mCopy(&mStorage, other.mInstance) : other.mInstance;
	}

	template <std::size_t SIZE>
	BasicAny<SIZE>::BasicAny(BasicAny &&other) : mCopy(other.mCopy), mMove(other.mMove), mDestroy(other.mDestroy), mType(other.mType)
	{
		if (other.mMove)
		{
			mInstance = other.mMove(&mStorage, other.mInstance);
			other.mDestroy = nullptr;
		}
		else
			mInstance = other.mInstance;
	}

	template <std::size_t SIZE>
	BasicAny<SIZE>::BasicAny(AnyRef handle) : BasicAny()
	{
		mInstance = handle.mInstance;
		mType = handle.mType;
	}

	template <std::size_t SIZE>
	BasicAny<SIZE>::~BasicAny()
	{
		if (mDestroy)
			mDestroy(mInstance);
	}

	template <std::size_t SIZE>
	template <typename T, typename U, typename>
	BasicAny<SIZE> &BasicAny<SIZE>::operator=(T &&object)
	{
		//Any temp(std::forward<T>(object));
		//Swap(temp)

		//return *this;

		return *this = BasicAny(std::forward<T>(object));
	}

	template <std::size_t SIZE>
	BasicAny<SIZE> &BasicAny<SIZE>::operator=(const BasicAny &other)
	{
		//Any temp(other);
		//Swap(temp);

		//return *this;

		return *this = BasicAny(other);
	}

	template <std::size_t SIZE>
	BasicAny<SIZE> &BasicAny<SIZE>::operator=(BasicAny &&other)
	{
		BasicAny temp(std::move(other));
		Swap(temp);

		return *this;
	}

	template <std::size_t SIZE>
	void BasicAny<SIZE>::Swap(BasicAny &other)
	{
		if (mMove && other.mMove)
		{
			Details::AlignedStorageT<SIZE> temp;
			void *instanceTemp = mMove(&temp, mInstance);
			mInstance = other.mMove(&mStorage, other.mInstance);
			other.mInstance = mMove(&other.mStorage, instanceTemp);
		}
		else if (mMove)
			other.mInstance = mMove(&other.mStorage, mInstance);
		else  if (other.mMove)
			mInstance = other.mMove(&mStorage, other.mInstance);
		else
			mInstance = other.mInstance;

		// const TypeDescriptor *typeTemp = mType;
		// mType = other.mType;
		// other.mType = typeTemp;
		std::swap(mType, other.mType);

		// void (*copyTemp)(void *, const void *) = mCopy;
		// mCopy = other.mCopy;
		// other.mCopy = copyTemp;
		std::swap(mCopy, other.mCopy);

		// void (*moveTemp)(void *, void *) = mMove;
		// mMove = other.mMove;
		// other.mMove = moveTemp;
		std::swap(mMove, other.mMove);

		// void (*destroyTemp)(void *) = mDestroy;
		// mDestroy = other.mDestroy;
		// other.mDestroy = destroyTemp;
		std::swap(mDestroy, other.mDestroy);
	}

	template <std::size_t SIZE>
	const TypeDescriptor *BasicAny<SIZE>::GetType() const
	{
		return mType;
	}

	template <std::size_t SIZE>
	const void *BasicAny<SIZE>::Get() const
	{
		return mInstance;
	}

	template <std::size_t SIZE>
	void *BasicAny<SIZE>::Get()
	{
		return const_cast<void*>(static_cast<const BasicAny&>(*this).Get());
		//return const_cast<void*>(std::as_const(*this).Get());
	}

	template <std::size_t SIZE>
	template <typename T>
	const T *BasicAny<SIZE>::TryCast() const
	{
		const TypeDescriptor *typeDesc = typeDesc = Details::Resolve<T>();
		
		void *casted = nullptr;

		if (!*this)
			return static_cast<T const*>(casted);

		if (typeDesc == mType)
			casted = mInstance;
		else
		{
			for (auto *base : mType->GetBases())
				if (base->GetType() == typeDesc)
					casted = base->Cast(mInstance);
		}

		return static_cast<T const*>(casted);
	}

	template <std::size_t SIZE>
	template <typename T>
	T *BasicAny<SIZE>::TryCast()
	{
		return const_cast<T*>(static_cast<const BasicAny&>(*this).TryCast<T>());
		//return const_cast<T*>(std::as_const(*this).TryCast<T>());
	}

	template <std::size_t SIZE>
	template <typename T>
	BasicAny<SIZE> BasicAny<SIZE>::TryConvert() const
	{
		BasicAny converted;

		if (!*this)
			return converted;

		if (TypeDescriptor const *typeDesc = Details::Resolve<T>(); typeDesc == mType)
			converted = *this;
		else
		{
			for (auto *conversion : mType->GetConversions())
				if (conversion->GetToType() == typeDesc)
					converted = conversion->Convert(mInstance);
		}

		return converted;
	}

}  // namespace Reflect

#endif  // META_ANY_H