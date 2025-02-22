// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/password_manager/core/browser/form_parsing/password_field_prediction.h"

#include <vector>

#include "base/stl_util.h"
#include "base/strings/utf_string_conversions.h"
#include "components/autofill/core/browser/form_structure.h"
#include "components/autofill/core/common/form_data.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "testing/gtest/include/gtest/gtest.h"

using autofill::ACCOUNT_CREATION_PASSWORD;
using autofill::AutofillField;
using autofill::CONFIRMATION_PASSWORD;
using autofill::EMAIL_ADDRESS;
using autofill::FormData;
using autofill::FormFieldData;
using autofill::FormStructure;
using autofill::NEW_PASSWORD;
using autofill::NO_SERVER_DATA;
using autofill::PASSWORD;
using autofill::ServerFieldType;
using autofill::SINGLE_USERNAME;
using autofill::UNKNOWN_TYPE;
using autofill::USERNAME;
using autofill::USERNAME_AND_EMAIL_ADDRESS;
using base::ASCIIToUTF16;

using FieldPrediction =
    autofill::AutofillQueryResponseContents::Field::FieldPrediction;

namespace password_manager {

namespace {

const PasswordFieldPrediction* FindFormPrediction(
    const FormPredictions& predictions,
    uint32_t renderer_id) {
  for (const PasswordFieldPrediction& prediction : predictions.fields) {
    if (prediction.renderer_id == renderer_id)
      return &prediction;
  }
  return nullptr;
}

TEST(FormPredictionsTest, ConvertToFormPredictions) {
  struct TestField {
    std::string name;
    std::string form_control_type;
    ServerFieldType input_type;
    ServerFieldType expected_type;
    bool may_use_prefilled_placeholder;
  } test_fields[] = {
      {"full_name", "text", UNKNOWN_TYPE, UNKNOWN_TYPE, false},
      // Password Manager is interested only in credential related types.
      {"Email", "email", EMAIL_ADDRESS, UNKNOWN_TYPE, false},
      {"username", "text", USERNAME, USERNAME, true},
      {"Password", "password", PASSWORD, PASSWORD, false},
      {"confirm_password", "password", CONFIRMATION_PASSWORD,
       CONFIRMATION_PASSWORD, true}};

  FormData form_data;
  for (size_t i = 0; i < base::size(test_fields); ++i) {
    FormFieldData field;
    field.unique_renderer_id = i + 1000;
    field.name = ASCIIToUTF16(test_fields[i].name);
    field.form_control_type = test_fields[i].form_control_type;
    form_data.fields.push_back(field);
  }

  FormStructure form_structure(form_data);
  size_t expected_predictions = 0;
  // Set server predictions and create expected votes.
  for (size_t i = 0; i < base::size(test_fields); ++i) {
    AutofillField* field = form_structure.field(i);
    field->set_server_type(test_fields[i].input_type);
    ServerFieldType expected_type = test_fields[i].expected_type;

    FieldPrediction prediction;
    prediction.set_may_use_prefilled_placeholder(
        test_fields[i].may_use_prefilled_placeholder);
    field->set_server_predictions({prediction});

    if (expected_type != UNKNOWN_TYPE)
      ++expected_predictions;
  }

  constexpr int driver_id = 1000;
  FormPredictions actual_predictions =
      ConvertToFormPredictions(driver_id, form_structure);

  // Check whether actual predictions are equal to expected ones.
  EXPECT_EQ(driver_id, actual_predictions.driver_id);
  EXPECT_EQ(form_structure.form_signature(), actual_predictions.form_signature);
  EXPECT_EQ(expected_predictions, actual_predictions.fields.size());

  for (size_t i = 0; i < base::size(test_fields); ++i) {
    uint32_t unique_renderer_id = form_data.fields[i].unique_renderer_id;
    const PasswordFieldPrediction* actual_prediction =
        FindFormPrediction(actual_predictions, unique_renderer_id);
    if (test_fields[i].expected_type == UNKNOWN_TYPE) {
      EXPECT_FALSE(actual_prediction);
    } else {
      ASSERT_TRUE(actual_prediction);
      EXPECT_EQ(test_fields[i].expected_type, actual_prediction->type);
      EXPECT_EQ(test_fields[i].may_use_prefilled_placeholder,
                actual_prediction->may_use_prefilled_placeholder);
    }
  }
}

TEST(FormPredictionsTest, ConvertToFormPredictions_SynthesiseConfirmation) {
  struct TestField {
    std::string name;
    std::string form_control_type;
    ServerFieldType input_type;
    ServerFieldType expected_type;
  };
  const std::vector<TestField> kTestForms[] = {
      {
          {"username", "text", USERNAME, USERNAME},
          {"new password", "password", ACCOUNT_CREATION_PASSWORD,
           ACCOUNT_CREATION_PASSWORD},
          // Same name and type means same signature. As a second new-password
          // field with this signature, the next field should be re-classified
          // to confirmation password.
          {"new password", "password", ACCOUNT_CREATION_PASSWORD,
           CONFIRMATION_PASSWORD},
      },
      {
          {"username", "text", USERNAME, USERNAME},
          {"new password duplicate", "password", ACCOUNT_CREATION_PASSWORD,
           ACCOUNT_CREATION_PASSWORD},
          // An explicit confirmation password above should override the
          // 2-new-passwords heuristic.
          {"new password duplicate", "password", ACCOUNT_CREATION_PASSWORD,
           ACCOUNT_CREATION_PASSWORD},
          {"confirm_password", "password", CONFIRMATION_PASSWORD,
           CONFIRMATION_PASSWORD},
      },
  };

  for (const std::vector<TestField>& test_form : kTestForms) {
    FormData form_data;
    for (size_t i = 0; i < test_form.size(); ++i) {
      FormFieldData field;
      field.unique_renderer_id = i + 1000;
      field.name = ASCIIToUTF16(test_form[i].name);
      field.form_control_type = test_form[i].form_control_type;
      form_data.fields.push_back(field);
    }

    FormStructure form_structure(form_data);
    // Set server predictions and create expected votes.
    for (size_t i = 0; i < test_form.size(); ++i) {
      AutofillField* field = form_structure.field(i);
      field->set_server_type(test_form[i].input_type);
    }

    FormPredictions actual_predictions =
        ConvertToFormPredictions(0 /*driver_id*/, form_structure);

    for (size_t i = 0; i < form_data.fields.size(); ++i) {
      SCOPED_TRACE(testing::Message()
                   << "field description: name=" << test_form[i].name
                   << ", form control type=" << test_form[i].form_control_type
                   << ", input type=" << test_form[i].input_type
                   << ", expected type=" << test_form[i].expected_type
                   << ", synthesised FormFieldData=" << form_data.fields[i]);
      const PasswordFieldPrediction* actual_prediction = FindFormPrediction(
          actual_predictions, form_data.fields[i].unique_renderer_id);
      ASSERT_TRUE(actual_prediction);
      EXPECT_EQ(test_form[i].expected_type, actual_prediction->type);
    }
  }
}

TEST(FormPredictionsTest, DeriveFromServerFieldType) {
  struct TestCase {
    const char* name;
    // Input.
    ServerFieldType server_type;
    CredentialFieldType expected_result;
  } test_cases[] = {
      {"No prediction", NO_SERVER_DATA, CredentialFieldType::kNone},
      {"Irrelevant type", EMAIL_ADDRESS, CredentialFieldType::kNone},
      {"Username", USERNAME, CredentialFieldType::kUsername},
      {"Username/Email", USERNAME_AND_EMAIL_ADDRESS,
       CredentialFieldType::kUsername},
      {"Single Username", SINGLE_USERNAME,
       CredentialFieldType::kSingleUsername},
      {"Password", PASSWORD, CredentialFieldType::kCurrentPassword},
      {"New password", NEW_PASSWORD, CredentialFieldType::kNewPassword},
      {"Account creation password", ACCOUNT_CREATION_PASSWORD,
       CredentialFieldType::kNewPassword},
      {"Confirmation password", CONFIRMATION_PASSWORD,
       CredentialFieldType::kConfirmationPassword},
  };

  for (const TestCase& test_case : test_cases) {
    SCOPED_TRACE(test_case.name);
    EXPECT_EQ(test_case.expected_result,
              DeriveFromServerFieldType(test_case.server_type));
  }
}

}  // namespace

}  // namespace password_manager
