#include <common/lambda_visitors.h>
#include <network/contentrestorator.hpp>
#include <ollama/json.hpp>
#include <ollama/ollama.hpp>

#include <algorithm>
#include <cstddef>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

#include <gtest/gtest.h>

namespace Testing {

class ContentRestoratorTest : public ::testing::Test
{
  public:
    inline static const std::vector<std::string_view> toFind = {"_A", "_BB", "_CCC"};

    inline static const std::string jsonCommon = R"({"message": { "content": "Hello, world!" } })";
    inline static const std::string jsonCommon2 = R"({"message": { "content": "1" } })";

    inline static const std::string jsonA = R"({"message": { "content": "_A" } })";
    inline static const std::string jsonB = R"({"message": { "content": "_BB" } })";
    inline static const std::string jsonC1 = R"({"message": { "content": "_CC" } })";
    inline static const std::string jsonC2 = R"({"message": { "content": "C23" } })";

    static ollama::response BuildChatResponse(const std::string &json_string)
    {
        return ollama::response{json_string, ollama::message_type::chat};
    }

    static std::size_t MaxToFindLength()
    {
        const auto it =
          std::max_element(toFind.begin(), toFind.end(), [](const auto &a, const auto &b) {
              return a.size() < b.size();
          });
        EXPECT_NE(it, toFind.end()) << "You set empty test, revise the code.";
        return (it != toFind.end()) ? it->size() : 0;
    }

    static std::string ExpectedContent(const std::string &jsonStr)
    {
        return nlohmann::json::parse(jsonStr)["message"]["content"];
    }

    inline static const std::size_t kMaxToFindLen{MaxToFindLength()};
    inline static const auto kEmptyResponse = BuildChatResponse("{}");
};

TEST_F(ContentRestoratorTest, TestNotFoundWithBigChunk)
{
    CContentRestorator restorer(toFind);
    const auto decision = restorer.Update(BuildChatResponse(jsonCommon));

    const LambdaVisitor visitor{[](const CContentRestorator::TPassToUser &) {
                                },
                                [](const auto &) {
                                    ADD_FAILURE();
                                }};
    std::visit(visitor, decision);
}

TEST_F(ContentRestoratorTest, TestNeedMoreDataWithSmallChunk)
{
    CContentRestorator restorer(toFind);
    const auto decision = restorer.Update(BuildChatResponse(jsonCommon2));

    const LambdaVisitor visitor{[](const CContentRestorator::TNeedMoreData &) {
                                },
                                [](const auto &) {
                                    ADD_FAILURE();
                                }};
    std::visit(visitor, decision);
}

TEST_F(ContentRestoratorTest, TestWithNotMatchingSmallChunk)
{
    // This is "main" test, it shows how it would be used.
    CContentRestorator restorer(toFind);

    for (std::size_t i = 1; i < kMaxToFindLen * 2; ++i)
    {
        const auto decision = restorer.Update(BuildChatResponse(jsonCommon2));
        const LambdaVisitor visitor{[&i](const CContentRestorator::TNeedMoreData &) {
                                        EXPECT_LT(i, kMaxToFindLen);
                                    },
                                    [&i](const CContentRestorator::TPassToUser &pass) {
                                        EXPECT_EQ(i, kMaxToFindLen);
                                        EXPECT_EQ(pass.collectedString,
                                                  std::string(kMaxToFindLen, '1'));
                                    },
                                    [&i](const CContentRestorator::TAlreadyDetected &) {
                                        EXPECT_GT(i, kMaxToFindLen);
                                    },
                                    [](const auto &) {
                                        ADD_FAILURE();
                                    }};
        std::visit(visitor, decision);
    }
}

TEST_F(ContentRestoratorTest, TestExactDetectionA)
{
    CContentRestorator restorer(toFind);
    const auto decision = restorer.Update(BuildChatResponse(jsonA));

    const LambdaVisitor visitor{[](const CContentRestorator::TDetected &det) {
                                    EXPECT_EQ(det.whatDetected, ExpectedContent(jsonA));
                                    EXPECT_EQ(det.collectedString, ExpectedContent(jsonA));
                                },
                                [](const auto &) {
                                    ADD_FAILURE();
                                }};
    std::visit(visitor, decision);
}

TEST_F(ContentRestoratorTest, TestExactDetectionB)
{
    CContentRestorator restorer(toFind);
    const auto decision = restorer.Update(BuildChatResponse(jsonB));

    const LambdaVisitor visitor{[](const CContentRestorator::TDetected &det) {
                                    EXPECT_EQ(det.whatDetected, ExpectedContent(jsonB));
                                    EXPECT_EQ(det.collectedString, ExpectedContent(jsonB));
                                },
                                [](const auto &) {
                                    ADD_FAILURE();
                                }};
    std::visit(visitor, decision);
}

TEST_F(ContentRestoratorTest, TestSplitDetection)
{
    CContentRestorator restorer(toFind);
    // "_CC" — not enough yet
    {
        const auto decision = restorer.Update(BuildChatResponse(jsonC1));
        const LambdaVisitor visitor{[](const CContentRestorator::TNeedMoreData &) {
                                    },
                                    [](const auto &) {
                                        ADD_FAILURE();
                                    }};
        std::visit(visitor, decision);
    }
    const auto decision = restorer.Update(BuildChatResponse(jsonC2)); // "C" → "_CCC"

    const LambdaVisitor visitor{[](const CContentRestorator::TDetected &det) {
                                    EXPECT_EQ(det.whatDetected, "_CCC");
                                    EXPECT_EQ(det.collectedString,
                                              ExpectedContent(jsonC1) + ExpectedContent(jsonC2));
                                },
                                [](const auto &) {
                                    ADD_FAILURE();
                                }};
    std::visit(visitor, decision);
}

TEST_F(ContentRestoratorTest, TestDetectionThenNewString)
{
    CContentRestorator restorer(toFind);

    const auto decision1 = restorer.Update(BuildChatResponse(jsonA));      // "_A"
    const auto decision2 = restorer.Update(BuildChatResponse(jsonCommon)); // обычный текст

    const LambdaVisitor visitor1{[](const CContentRestorator::TDetected &det) {
                                     EXPECT_EQ(det.whatDetected, ExpectedContent(jsonA));
                                     EXPECT_EQ(det.collectedString, ExpectedContent(jsonA));
                                 },
                                 [](const auto &) {
                                     ADD_FAILURE();
                                 }};
    std::visit(visitor1, decision1);

    const LambdaVisitor visitor2{[](const CContentRestorator::TAlreadyDetected &) {
                                 },
                                 [](const auto &) {
                                     ADD_FAILURE();
                                 }};
    std::visit(visitor2, decision2);
}

TEST_F(ContentRestoratorTest, TestResetWorks)
{
    CContentRestorator restorer(toFind);
    {
        const auto decision = restorer.Update(BuildChatResponse(jsonA));
        const LambdaVisitor visitor{[](const CContentRestorator::TDetected &det) {
                                        EXPECT_EQ(det.whatDetected, ExpectedContent(jsonA));
                                    },
                                    [](const auto &) {
                                        ADD_FAILURE();
                                    }};
        std::visit(visitor, decision);
    }
    {
        SCOPED_TRACE("It should be locked as already detected.");
        const auto decision = restorer.Update(BuildChatResponse(jsonCommon));
        const LambdaVisitor visitor{[](const CContentRestorator::TAlreadyDetected &) {
                                    },
                                    [](const auto &) {
                                        ADD_FAILURE();
                                    }};
        std::visit(visitor, decision);
    }

    restorer.Reset();
    {
        SCOPED_TRACE("After Reset, the restored state should behave normally: ordinary text leads "
                     "to PassToUser.");
        const auto decision = restorer.Update(BuildChatResponse(jsonCommon));
        const LambdaVisitor visitor{[](const CContentRestorator::TPassToUser &pass) {
                                        EXPECT_EQ(pass.collectedString,
                                                  ExpectedContent(jsonCommon));
                                    },
                                    [](const auto &) {
                                        ADD_FAILURE();
                                    }};
        std::visit(visitor, decision);
    }
}

TEST_F(ContentRestoratorTest, TestEmptyJson)
{
    CContentRestorator restorer(toFind);
    const auto decision = restorer.Update(kEmptyResponse);

    const LambdaVisitor visitor{[](const CContentRestorator::TNeedMoreData &) {
                                },
                                [](const auto &) {
                                    ADD_FAILURE();
                                }};
    std::visit(visitor, decision);
}

TEST_F(ContentRestoratorTest, TestWithPartialAndEmptyData_CCC)
{
    CContentRestorator restorer(toFind);

    // "_CC" — not enough yet
    const auto decision1 = restorer.Update(BuildChatResponse(jsonC1));
    const LambdaVisitor visitor1{[](const CContentRestorator::TNeedMoreData &) {
                                 },
                                 [](const auto &) {
                                     ADD_FAILURE();
                                 }};
    std::visit(visitor1, decision1);

    // Empty chunk
    const auto decision2 = restorer.Update(kEmptyResponse); // empty data
    const LambdaVisitor visitor2{[](const CContentRestorator::TNeedMoreData &) {
                                 },
                                 [](const auto &) {
                                     ADD_FAILURE();
                                 }};
    std::visit(visitor2, decision2);

    // "_CCC" should be detected after "_CC" + empty + "23"
    const auto decision3 = restorer.Update(BuildChatResponse(jsonC2)); // "_CCC23"
    const LambdaVisitor visitor3{[](const CContentRestorator::TDetected &det) {
                                     EXPECT_EQ(det.whatDetected, "_CCC");
                                     EXPECT_EQ(det.collectedString,
                                               ExpectedContent(jsonC1) + ExpectedContent(jsonC2));
                                 },
                                 [](const auto &) {
                                     ADD_FAILURE();
                                 }};
    std::visit(visitor3, decision3);

    const auto decision4 = restorer.Update(kEmptyResponse);
    const LambdaVisitor visitor4{[](const CContentRestorator::TAlreadyDetected &) {
                                 },
                                 [](const auto &) {
                                     ADD_FAILURE();
                                 }};
    std::visit(visitor4, decision4);
}

} // namespace Testing
