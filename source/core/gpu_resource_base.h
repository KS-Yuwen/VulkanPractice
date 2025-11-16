#pragma once
#include <memory>

template<typename T>
class GpuResourceBase
{
public:
	// コピーは禁止
	GpuResourceBase(const GpuResourceBase&) = delete;
	GpuResourceBase& operator= (const GpuResourceBase&) = delete;

	virtual ~GpuResourceBase() = default;

	static std::shared_ptr<T> Create() { return std::shared_ptr<T>(new T()); }

protected:
	GpuResourceBase() = default;	// Create関数で作成を制限するため
};