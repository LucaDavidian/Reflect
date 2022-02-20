#ifndef CONSTRUCTOR_H
#define CONSTRUCTOR_H

#include <vector>
#include <tuple>
#include "TypeDescriptor.hpp"
#include "Any.hpp"
#include "Conversion.hpp"
#include "Base.hpp"

namespace Reflect
{

	inline bool CanCastOrConvert(const TypeDescriptor *from, const TypeDescriptor *to)
	{
		if (from == to)
			return true;

		for (auto *base : from->GetBases())
			if (base->GetType() == to)
				return true;

		for (auto *conversion : from->GetConversions())
			if (conversion->GetToType() == to)
				return true;

		return false;
	}

	class Constructor
	{
	public:
		Any NewInstance(std::vector<Any> &args)
		{
			if (args.size() == mParamTypes.size())
				return NewInstanceImpl(args);

			return Any();
		}

		template <typename... Args>
		Any NewInstance(Args&&... args) const
		{
			if (sizeof...(Args) == mParamTypes.size())
			{
				auto argsAny = std::vector<Any>({ Any(std::forward<Args>(args))... });
				return NewInstanceImpl(argsAny);
			}

			return Any();
		}

		TypeDescriptor const *GetParent() const
		{
			return mParent;
		}

		TypeDescriptor const *GetParamType(size_t index) const
		{
			return mParamTypes[index];
		}

		size_t GetNumParams() const
		{
			return mParamTypes.size();
		}

		template <typename... Args, size_t... indices>
		bool CanConstruct(std::index_sequence<indices...> indexSequence = std::index_sequence_for<Args...>()) const
		{
			return GetNumParams() == sizeof...(Args) && ((Reflect::CanCastOrConvert(Details::Resolve<Args>(), GetParamType(indices))) && ...);
		}

	protected:
		Constructor(TypeDescriptor *parent, const std::vector<const TypeDescriptor*> &paramTypes) : mParent(parent), mParamTypes(paramTypes) {}

	private:
		virtual Any NewInstanceImpl(std::vector<Any> &args) const = 0;

		TypeDescriptor *mParent;
		std::vector<TypeDescriptor const*> mParamTypes;
	};

	template <typename Type, typename... Args>
	class ConstructorImpl : public Constructor
	{
	public:
		ConstructorImpl() : Constructor(Details::Resolve<Details::RawType<Type>>(), { Details::Resolve<Details::RawType<Args>>()... }) {}

	private:
		Any NewInstanceImpl(std::vector<Any> &args) const override
		{
			return NewInstanceImpl(args, std::make_index_sequence<sizeof...(Args)>());
		}

		template <size_t... indices>
		Any NewInstanceImpl(std::vector<Any> &args, std::index_sequence<indices...> indexSequence) const
		{
			std::tuple argsTuple = std::make_tuple(args[indices].TryCast<std::remove_cv_t<std::remove_reference_t<Args>>>()...);
			std::vector<Any> convertedArgs{ (std::get<indices>(argsTuple) ? AnyRef(*std::get<indices>(argsTuple)) : args[indices].TryConvert<std::remove_cv_t<std::remove_reference_t<Args>>>())... };
			argsTuple = std::make_tuple(convertedArgs[indices].TryCast<std::remove_cv_t<std::remove_reference_t<Args>>>()...);

			if ((std::get<indices>(argsTuple) && ...))
				return Type(*std::get<indices>(argsTuple)...);

			return Any();
		}
	};

	template <typename Type, typename... Args>
	class FreeFunConstructor : public Constructor
	{
	private:
		typedef Type(*CtorFun)(Args...);
	public:
		FreeFunConstructor(CtorFun ctorFun) : Constructor(Details::Resolve<Details::RawType<Type>>(), { Details::Resolve<Details::RawType<Args>>()... }), mCtorFun(ctorFun) {}
	
	private:
		Any NewInstanceImpl(std::vector<Any> &args) const override
		{
			return NewInstanceImpl(args, std::make_index_sequence<sizeof...(Args)>());
		}

		template <size_t... indices>
		Any NewInstanceImpl(std::vector<Any> &args, std::index_sequence<indices...> indexSequence) const
		{
			std::tuple argsTuple = std::make_tuple(args[indices].TryCast<std::remove_cv_t<std::remove_reference_t<Args>>>()...);
			std::vector<Any> convertedArgs{ (std::get<indices>(argsTuple) ? AnyRef(*std::get<indices>(argsTuple)) : args[indices].TryConvert<std::remove_cv_t<std::remove_reference_t<Args>>>())... };
			argsTuple = std::make_tuple(convertedArgs[indices].TryCast<std::remove_cv_t<std::remove_reference_t<Args>>>()...);

			if ((std::get<indices>(argsTuple) && ...))
				return mCtorFun(*std::get<indices>(argsTuple)...);

			return Any();
		}
		
		CtorFun mCtorFun;
	};

}  // namespace Reflect

#endif // CONSTRUCTOR_H
