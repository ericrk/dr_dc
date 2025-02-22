// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef COMPONENTS_AUTOFILL_ASSISTANT_BROWSER_ACTIONS_USE_ADDRESS_ACTION_H_
#define COMPONENTS_AUTOFILL_ASSISTANT_BROWSER_ACTIONS_USE_ADDRESS_ACTION_H_

#include <memory>
#include <string>
#include <vector>

#include "base/callback.h"
#include "base/macros.h"
#include "base/memory/weak_ptr.h"
#include "components/autofill_assistant/browser/actions/action.h"
#include "components/autofill_assistant/browser/batch_element_checker.h"

namespace autofill {
class AutofillProfile;
}  // namespace autofill

namespace autofill_assistant {
class ClientStatus;

// An action to autofill a form using a local address.
class UseAddressAction : public Action {
 public:
  explicit UseAddressAction(ActionDelegate* delegate, const ActionProto& proto);
  ~UseAddressAction() override;

 private:
  enum FieldValueStatus { UNKNOWN, EMPTY, NOT_EMPTY };
  struct RequiredField {
    Selector selector;
    bool simulate_key_presses = false;
    int delay_in_millisecond = 0;
    bool forced = false;
    FieldValueStatus status = UNKNOWN;

    // When filling in address, address_field must be set.
    UseAddressProto::RequiredField::AddressField address_field =
        UseAddressProto::RequiredField::UNDEFINED;

    // Returns true if fallback is required for this field.
    bool ShouldFallback(bool has_fallback_data) const {
      return status == EMPTY || (forced && has_fallback_data);
    }
  };

  // Data necessary for filling in the fallback fields. This is kept in a
  // separate struct to make sure we don't keep it for longer than strictly
  // necessary.
  struct FallbackData {
    FallbackData() = default;
    ~FallbackData() = default;

    // Profile for UseAddress fallback.
    const autofill::AutofillProfile* profile = nullptr;

   private:
    DISALLOW_COPY_AND_ASSIGN(FallbackData);
  };

  // Overrides Action:
  void InternalProcessAction(ProcessActionCallback callback) override;

  void EndAction(ProcessedActionStatusProto status);
  void EndAction(const ClientStatus& status);

  // Fill the form using data in client memory. Return whether filling succeeded
  // or not through OnFormFilled.
  void FillFormWithData();
  void OnWaitForElement(const ClientStatus& element_status);

  // Called when the address has been filled.
  void OnFormFilled(std::unique_ptr<FallbackData> fallback_data,
                    const ClientStatus& status);

  // Check whether all required fields have a non-empty value. If it is the
  // case, finish the action successfully. If it's not and |fallback_data|
  // is null, fail the action. If |fallback_data| is non-null, use it to attempt
  // to fill the failed fields without Autofill.
  void CheckRequiredFields(std::unique_ptr<FallbackData> fallback_data);

  // Triggers the check for a specific field.
  void CheckRequiredFieldsSequentially(
      bool allow_fallback,
      size_t required_fields_index,
      std::unique_ptr<FallbackData> fallback_data);

  // Updates |required_fields_value_status_|.
  void OnGetRequiredFieldValue(size_t required_fields_index,
                               const ClientStatus& element_status,
                               const std::string& value);

  // Called when all required fields have been checked.
  void OnCheckRequiredFieldsDone(std::unique_ptr<FallbackData> fallback_data);

  // Gets the fallback value.
  std::string GetFallbackValue(const RequiredField& required_field,
                               const FallbackData& fallback_data);

  // Get the value of |address_field| associated to profile |profile|. Return
  // empty string if there is no data available.
  base::string16 GetAddressFieldValue(
      const autofill::AutofillProfile* profile,
      const UseAddressProto::RequiredField::AddressField& address_field);

  // Sets fallback field values for empty fields from
  // |required_fields_value_status_|.
  void SetFallbackFieldValuesSequentially(
      size_t required_fields_index,
      std::unique_ptr<FallbackData> fallback_data);

  // Called after trying to set form values without Autofill in case of fallback
  // after failed validation.
  void OnSetFallbackFieldValue(size_t required_fields_index,
                               std::unique_ptr<FallbackData> fallback_data,
                               const ClientStatus& status);

  // Usage of the autofilled address.
  std::string name_;
  std::string prompt_;
  Selector selector_;

  std::vector<RequiredField> required_fields_;

  std::unique_ptr<BatchElementChecker> batch_element_checker_;

  ProcessActionCallback process_action_callback_;
  base::WeakPtrFactory<UseAddressAction> weak_ptr_factory_{this};

  DISALLOW_COPY_AND_ASSIGN(UseAddressAction);
};

}  // namespace autofill_assistant
#endif  // COMPONENTS_AUTOFILL_ASSISTANT_BROWSER_ACTIONS_AUTOFILL_ACTION_H_
