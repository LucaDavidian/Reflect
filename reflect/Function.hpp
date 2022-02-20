#ifndef MEMBER_FUNCTION_H
#define MEMBER_FUNCTION_H

#include <string>
#include <vector>
#include <tuple>
#include "TypeDescriptor.hpp"
#include "Any.hpp"

namespace Reflect
{

	class Function
	{
	public:
		std::string GetName() const { return mName; }
		const TypeDescriptor *GetParent() const { return mParent; }

		template <typename... Args>
		Any Invoke(AnyRef object, Args&&... args) const
		{
			if (sizeof...(Args) == mParamTypes.size())
			{
				std::vector<Any> anyArgs{ Any(std::forward<Args>(args))... };

				return InvokeImpl(object, anyArgs);
			}

			return Any();
		}

		const TypeDescriptor *GetReturnType() const
		{
			return mReturnType;
		}

		std::vector<const TypeDescriptor*> GetParamTypes() const
		{
			return mParamTypes;
		}

		const TypeDescriptor *GetParamType(size_t index) const
		{
			return mParamTypes[index];
		}

		std::size_t GetNumParams() const
		{
			return mParamTypes.size();
		}

	protected:
		Function(const std::string &name, const TypeDescriptor *parent, const TypeDescriptor *returnType, const std::vector<TypeDescriptor const*> paramTypes)
			: mName(name), mParent(parent), mReturnType(returnType), mParamTypes(paramTypes) {}

		const TypeDescriptor *mReturnType;
		std::vector<TypeDescriptor const *> mParamTypes;

	private:
		virtual Any InvokeImpl(Any object, std::vector<Any> &args) const = 0;

		std::string mName;
		TypeDescriptor const *const mParent;
	};


	template <typename Ret, typename... Args>
	class FreeFunction : public Function
	{
	private:
		using FunPtr = Ret(*)(Args...);

	public:
		FreeFunction(FunPtr freeFunPtr, const std::string &name)
			: Function(name, nullptr, Details::Resolve<Ret>(), { Details::Resolve<std::remove_cv_t<std::remove_reference_t<Args>>>()... }), mFreeFunPtr(freeFunPtr) {}

	private:
		Any InvokeImpl(Any, std::vector<Any> &args) const override
		{
			return InvokeImpl(args, std::index_sequence_for<Args...>());
		}

		template <size_t... indices>
		Any InvokeImpl(std::vector<Any> &args, std::index_sequence<indices...> indexSequence) const
		{
			std::tuple argsTuple = std::make_tuple(args[indices].TryCast<std::remove_cv_t<std::remove_reference_t<Args>>>()...);
			std::vector<Any> convertedArgs{ (std::get<indices>(argsTuple) ? AnyRef(*std::get<indices>(argsTuple)) : args[indices].TryConvert<std::remove_cv_t<std::remove_reference_t<Args>>>())... };
			argsTuple = std::make_tuple(convertedArgs[indices].TryCast<std::remove_cv_t<std::remove_reference_t<Args>>>()...);

			if ((std::get<indices>(argsTuple) && ...))  // all arguments are valid
				if constexpr (std::is_reference_v<Ret>)
					return AnyRef(mFreeFunPtr(*std::get<indices>(argsTuple)...));
				else
					return mFreeFunPtr(*std::get<indices>(argsTuple)...);
			else
				return Any();
		}

		FunPtr mFreeFunPtr;
	};


	template <typename... Args>
	class FreeFunction<void, Args...> : public Function
	{
	private:
		using FunPtr = void(*)(Args...);

	public:
		FreeFunction(FunPtr freeFunPtr, const std::string &name)
			: Function(name, nullptr, Details::Resolve<void>(), { Details::Resolve<std::remove_cv_t<std::remove_reference_t<Args>>>()... }),
			mFreeFunPtr(freeFunPtr) {}

	private:
		Any InvokeImpl(Any, std::vector<Any> &args) const override
		{
			return InvokeImpl(args, std::index_sequence_for<Args...>());
		}

		template <size_t... indices>
		Any InvokeImpl(std::vector<Any> &args, std::index_sequence<indices...> indexSequence) const
		{
			std::tuple argsTuple = std::make_tuple(args[indices].TryCast<std::remove_cv_t<std::remove_reference_t<Args>>>()...);
			std::vector<Any> convertedArgs{ (std::get<indices>(argsTuple) ? AnyRef(*std::get<indices>(argsTuple)) : args[indices].TryConvert<std::remove_cv_t<std::remove_reference_t<Args>>>())... };
			argsTuple = std::make_tuple(convertedArgs[indices].TryCast<std::remove_cv_t<std::remove_reference_t<Args>>>()...);

			if ((std::get<indices>(argsTuple) && ...))  // all arguments are valid
				mFreeFunPtr(*std::get<indices>(argsTuple)...);

			return Any();
		}

		FunPtr mFreeFunPtr;
	};


	template <typename C, typename Ret, typename... Args>
	class MemberFunction : public Function
	{
	private:
		using MemFunPtr = Ret(C::*)(Args...);

	public:
		MemberFunction(MemFunPtr memFun, const std::string &name)
			: Function(name, Details::Resolve<C>(), Details::Resolve<Ret>(), { Details::Resolve<std::remove_cv_t<std::remove_reference_t<Args>>>()... }), mMemFunPtr(memFun) {}

	private:
		Any InvokeImpl(Any object, std::vector<Any> &args) const override
		{
			return InvokeImpl(object, args, std::make_index_sequence<sizeof...(Args)>());
		}

		template <size_t... indices>
		Any InvokeImpl(Any object, std::vector<Any> &args, std::index_sequence<indices...> indexSequence) const
		{
			std::tuple argsTuple = std::make_tuple(args[indices].TryCast<std::remove_cv_t<std::remove_reference_t<Args>>>()...);
			std::vector<Any> convertedArgs{ (std::get<indices>(argsTuple) ? AnyRef(*std::get<indices>(argsTuple)) : args[indices].TryConvert<std::remove_cv_t<std::remove_reference_t<Args>>>())... };
			argsTuple = std::make_tuple(convertedArgs[indices].TryCast<std::remove_cv_t<std::remove_reference_t<Args>>>()...);

			if (C *obj = object.TryCast<C>(); (std::get<indices>(argsTuple) && ...) && obj)  // object is valid and all arguments are valid 
				if constexpr (std::is_reference_v<Ret>)
					return AnyRef((obj->*mMemFunPtr)(*std::get<indices>(argsTuple)...));
				else
					return (obj->*mMemFunPtr)(*std::get<indices>(argsTuple)...);
			else
				return Any();
		}

		MemFunPtr mMemFunPtr;
	};


	template <typename C, typename... Args>
	class MemberFunction<C, void, Args...> : public Function
	{
	private:
		using MemFunPtr = void(C::*)(Args...);

	public:
		MemberFunction(MemFunPtr memFun, const std::string &name)
			: Function(name, Details::Resolve<C>(), Details::Resolve<void>(), { Details::Resolve<std::remove_cv_t<std::remove_reference_t<Args>>>()... }),
			mMemFunPtr(memFun) {}

	private:
		Any InvokeImpl(Any object, std::vector<Any> &args) const override
		{
			return InvokeImpl(object, args, std::make_index_sequence<sizeof...(Args)>());
		}

		template <size_t... indices>
		Any InvokeImpl(Any object, std::vector<Any> &args, std::index_sequence<indices...> indexSequence) const
		{
			std::tuple argsTuple = std::make_tuple(args[indices].TryCast<std::remove_cv_t<std::remove_reference_t<Args>>>()...);
			std::vector<Any> convertedArgs{ (std::get<indices>(argsTuple) ? AnyRef(*std::get<indices>(argsTuple)) : args[indices].TryConvert<std::remove_cv_t<std::remove_reference_t<Args>>>())... };
			argsTuple = std::make_tuple(convertedArgs[indices].TryCast<std::remove_cv_t<std::remove_reference_t<Args>>>()...);

			if (C *obj = object.TryCast<C>(); (std::get<indices>(argsTuple) && ...) && obj)  // object is valid and all arguments are valid 
				(obj->*mMemFunPtr)(*std::get<indices>(argsTuple)...);

			return Any();
		}

		MemFunPtr mMemFunPtr;
	};


	template <typename C, typename Ret, typename... Args>
	class ConstMemberFunction : public Function
	{
	private:
		using ConstMemFunPtr = Ret(C::*)(Args...) const;

	public:
		ConstMemberFunction(ConstMemFunPtr constMemFun, const std::string &name)
			: Function(name, Details::Resolve<C>(), Details::Resolve<Ret>(), { Details::Resolve<std::remove_cv_t<std::remove_reference_t<Args>>>()... }), mConstMemFunPtr(constMemFun) {}

	private:
		Any InvokeImpl(Any object, std::vector<Any> &args) const override
		{
			return InvokeImpl(object, args, std::make_index_sequence<sizeof...(Args)>());
		}

		template <size_t... indices>
		Any InvokeImpl(Any object, std::vector<Any> &args, std::index_sequence<indices...> indexSequence) const
		{
			std::tuple argsTuple = std::make_tuple(args[indices].TryCast<std::remove_cv_t<std::remove_reference_t<Args>>>()...);
			std::vector<Any> convertedArgs{ (std::get<indices>(argsTuple) ? AnyRef(*std::get<indices>(argsTuple)) : args[indices].TryConvert<std::remove_cv_t<std::remove_reference_t<Args>>>())... };
			argsTuple = std::make_tuple(convertedArgs[indices].TryCast<std::remove_cv_t<std::remove_reference_t<Args>>>()...);

			if (C *obj = object.TryCast<C>(); obj && (std::get<indices>(argsTuple) && ...))  // object is valid and all arguments are valid 
				if constexpr (std::is_void<Ret>::value)
				{
					(obj->*mConstMemFunPtr)(*std::get<indices>(argsTuple)...);

					return Any();
				}
				else
					if constexpr (std::is_reference_v<Ret>)
						return AnyRef((obj->*mConstMemFunPtr)(*std::get<indices>(argsTuple)...));
					else
						return (obj->*mConstMemFunPtr)(*std::get<indices>(argsTuple)...);
			else
				return Any();
		}

		ConstMemFunPtr mConstMemFunPtr;
	};

}  // namespace Reflect

#endif  // MEMBER_FUNCTION_H