#ifndef DATA_MEMBER_H
#define DATA_MEMBER_H

#include "TypeDescriptor.hpp"
#include "Any.hpp"
#include <string>

namespace Reflect
{

	class DataMember
	{
	public:
		std::string GetName() const { return mName; }
		const TypeDescriptor *GetParent() const { return mParent; }
		const TypeDescriptor *GetType() const { return mType; }

		virtual void Set(AnyRef objectRef, const Any value) = 0;
		virtual Any Get(Any object) = 0;

	protected:
		DataMember(const std::string &name, const TypeDescriptor *type, const TypeDescriptor *parent)
			: mName(name), mType(type), mParent(parent) {}

	private:
		std::string mName;                 
		const TypeDescriptor *mType;    // type of the data member
		const TypeDescriptor *mParent;  // type of the data member's class
	};

	template <typename Class, typename Type>
	class PtrDataMember : public DataMember
	{
	public:
		PtrDataMember(Type Class::*dataMemberPtr, const std::string name)
			: DataMember(name, Details::Resolve<Type>(), Details::Resolve<Class>()), mDataMemberPtr(dataMemberPtr) {}

		// void Set(AnyRef objectRef, const Any value) override
		// {
		// 	SetImpl(objectRef, value);  // use SFINAE
		// }

		void Set(AnyRef objectRef, const Any value) override
		{
			SetImpl(objectRef, value, std::is_const<Type>());  // use tag dispatch
		}

		Any Get(Any object) override
		{
			Class *obj = object.TryCast<Class>();

			if (!obj)
				throw BadCastException(Details::Resolve<Class>()->GetName(), object.GetType()->GetName());

			return obj->*mDataMemberPtr;
		}

	private:
		Type Class::*mDataMemberPtr;

		////// use SFINAE
		// template <typename U = Type, typename = typename std::enable_if<!std::is_const<U>::value>::type>
		// void SetImpl(Any object, const Any value)
		// {
		// 	Class *obj = object.TryCast<Class>();
		// 	if (!obj)
		// 		throw BadCastException(Details::Resolve<Type>()->GetName(), object.GetType()->GetName(), "object:");
	
		// 	Any val = value.TryConvert<Type>();
	
		// 	if (!val)
		// 		throw BadCastException(Details::Resolve<Type>()->GetName(), value.GetType()->GetName(), "value:");
		// 	obj->*mDataMemberPtr = val.TryCast<Type>();
		// }

		// template <typename U = Type, typename = typename std::enable_if<std::is_const<U>::value>::type, typename = void>
		// void SetImpl(Any object, const Any value)
		// {
		// 	static_assert(false, "can't set const data member");
		// }

		////// use tag dispatch
		void SetImpl(Any object, const Any value, std::false_type)
		{
			Class *obj = object.TryCast<Class>();  // pointers to members of base class can be used with derived class

			if (!obj)
				throw BadCastException(Details::Resolve<Class>()->GetName(), object.GetType()->GetName(), "object:");

			Type const *casted = nullptr;
			Any val;
			if (casted = value.TryCast<Type>(); !casted)
			{
				val = value.TryConvert<Type>();
				casted = val.TryCast<Type>();
			}

			if (!casted)
				throw BadCastException(Details::Resolve<Type>()->GetName(), value.GetType()->GetName(), "value:");

			obj->*mDataMemberPtr = *casted;
		}

		void SetImpl(Any object, const Any value, std::true_type)
		{
			//static_assert(false, "can't set const data member");
		}
	};

	// helper meta function to get info about functions passed as auto non type params (C++17)
	template <typename>
	struct FunctionHelper;

	template <typename Ret, typename... Args>
	struct FunctionHelper<Ret(Args...)>
	{
		using ReturnType = Ret;
		using ParamsTypes = std::tuple<Args...>;
	};

	template <typename Class, typename Ret, typename... Args>
	/*constexpr*/ FunctionHelper<Ret(/*Class, */Args...)> ToFunctionHelper(Ret(Class::*)(Args...));

	template <typename Class, typename Ret, typename... Args>
	/*constexpr*/ FunctionHelper<Ret(/*Class, */Args...)> ToFunctionHelper(Ret(Class::*)(Args...) const);

	template <typename Ret, typename... Args>
	/*constexpr*/ FunctionHelper<Ret(Args...)> ToFunctionHelper(Ret(*)(Args...));

	template <auto Setter, auto Getter, typename Class>
	class SetGetDataMember : public DataMember
	{
	private:
		using MemberType = Details::RawType<typename decltype(ToFunctionHelper(Getter))::ReturnType>;

	public:
		SetGetDataMember(const std::string name)
			: DataMember(name, Details::Resolve<MemberType>(), Details::Resolve<Class>()) {}

		void Set(AnyRef objectRef, const Any value) override
		{
			Any a = objectRef;
			Class *obj = a.TryCast<Class>();

			if (!obj)
				throw BadCastException(Details::Resolve<Class>()->GetName(), Any(objectRef).GetType()->GetName(), "object:");

			MemberType const *casted = nullptr;
			Any val;
			if (casted = value.TryCast<MemberType>(); !casted)
			{
				val = value.TryConvert<MemberType>();
				casted = val.TryCast<MemberType>();
			}

			if (!casted)
				throw BadCastException(Details::Resolve<MemberType>()->GetName(), value.GetType()->GetName(), "value:");

			if constexpr (std::is_member_function_pointer_v<decltype(Setter)>)
				(obj->*Setter)(*casted);
			else
			{
				static_assert(std::is_function_v<std::remove_pointer_t<decltype(Setter)>>);

				Setter(*obj, *casted);
			}

		}

		Any Get(Any object) override
		{
			Class *obj = object.TryCast<Class>();

			if (!obj)
				throw BadCastException(Details::Resolve<Class>()->GetName(), object.GetType()->GetName());

			if constexpr (std::is_member_function_pointer_v<decltype(Setter)>)
				return (obj->*Getter)();
			else
			{
				static_assert(std::is_function_v<std::remove_pointer_t<decltype(Getter)>>);

				return Getter(*obj);
			}
		}
	};

}  // namespace Reflect

#endif // DATA_MEMBER_H
