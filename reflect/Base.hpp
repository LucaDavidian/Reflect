#ifndef BASE_H
#define BASE_H

#include "TypeDescriptor.hpp"

namespace Reflect
{

	class Base
	{
	public:
		const TypeDescriptor *GetType() const { return mType; }

		virtual void *Cast(void *object) = 0;

	protected:
		Base(TypeDescriptor const *type, const TypeDescriptor *parent)
			: mParent(parent), mType(type) {}

	private:
		const TypeDescriptor *mParent;
		const TypeDescriptor *mType;
	};

	template <typename B, typename D>
	class BaseImpl : public Base
	{
	public:
		BaseImpl() : Base(Details::Resolve<B>(), Details::Resolve<D>()) {}

		void *Cast(void *object) override
		{
			return static_cast<B*>(object);
		}
	};

}  // namespace Reflect

#endif // BASE_H

