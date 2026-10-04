#pragma once

#include "input_scheme.h"
#include "../shuangpin/shuangpin_profile.h"
#include <string>
#include <vector>

class ShuangpinScheme : public IInputScheme
{
  public:
    explicit ShuangpinScheme(const ShuangpinProfile &profile = GetXiaoheShuangpinProfile());
    void reset() override;
    void handle_key(ImeKeyCode vk, ImeModifierMask modifiers_down, ImeCharacter wch) override;
    QueryRequest build_request() const override;
    std::string get_preedit() const override;
    SchemeType type() const override;
    void set_raw_input(const std::string &raw_input, const std::string &raw_input_with_cases) override;
    // 直接辅助码开着时收 /（四码后的终止键），; 韵母也不再要求所在一节是奇数键：辅码会打乱奇偶。
    void set_direct_helpcode(bool enabled)
    {
        direct_helpcode_ = enabled;
    }

  private:
    const ShuangpinProfile profile_;
    bool direct_helpcode_ = false;
    std::string raw_input_;
    std::vector<KeyStroke> key_strokes_;
};
