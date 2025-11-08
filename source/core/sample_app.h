#pragma once
class ISampleApp
{
public:
	virtual ~ISampleApp() = default;
	virtual void OnInitialize() = 0;
	virtual void OnDrawFrame() = 0;
	virtual void OnCleanup() = 0;
};
