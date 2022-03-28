#include <Functions/IFunction.h>
#include <Functions/FunctionFactory.h>
#include <Functions/FunctionsConversion.h>
#include <Functions/FunctionsLogical.h>
#include <DataTypes/DataTypeDate.h>
#include <DataTypes/DataTypesNumber.h>
#include <Columns/ColumnsNumber.h>

#include <Common/DateLUT.h>
#include <Common/assert_cast.h>
#include <Common/typeid_cast.h>

#include <array>

namespace DB
{
namespace ErrorCodes
{
    extern const int NUMBER_OF_ARGUMENTS_DOESNT_MATCH;
    extern const int ILLEGAL_TYPE_OF_ARGUMENT;
}

namespace
{

// A helper function to simplify comparisons of valid YYYY-MM-DD values for <,>,=
inline constexpr Int64 YearMonthDayToSingleInt(Int64 year, Int64 month, Int64 day)
{
    return year*512 + month*32 + day;
}

// Common implementation for makeDate, makeDate32
template <typename Traits>
class FunctionMakeDate : public IFunction
{
private:
    static constexpr std::array<const char*, 3> argument_names = {"year", "month", "day"};

public:
    static constexpr auto name = Traits::name;

    static FunctionPtr create(ContextPtr) { return std::make_shared<FunctionMakeDate>(); }

    String getName() const override { return name; }

    bool isVariadic() const override { return false; }

    size_t getNumberOfArguments() const override { return argument_names.size(); }

    bool isInjective(const ColumnsWithTypeAndName &) const override
    {
        return false; // {year,month,day} that are out of supported range are converted into a default value
    }

    bool isSuitableForShortCircuitArgumentsExecution(const DataTypesWithConstInfo & /*arguments*/) const override { return true; }

    bool useDefaultImplementationForNulls() const override { return true; }

    bool useDefaultImplementationForConstants() const override { return true; }

    DataTypePtr getReturnTypeImpl(const DataTypes & arguments) const override
    {
        if (arguments.size() != argument_names.size())
            throw Exception(ErrorCodes::NUMBER_OF_ARGUMENTS_DOESNT_MATCH,
                "Function {} requires 3 arguments, but {} given", getName(), arguments.size());

        for (size_t i = 0; i < argument_names.size(); ++i)
        {
            DataTypePtr argument_type = arguments[i];
            if (!isNumber(argument_type) && !isStringOrFixedString(argument_type))
                throw Exception("Argument '" + std::string(argument_names[i]) + "' for function " + getName() + " must be number",
                    ErrorCodes::ILLEGAL_TYPE_OF_ARGUMENT);
        }

        return std::make_shared<typename Traits::ReturnDataType>();
    }

    ColumnPtr executeImpl(const ColumnsWithTypeAndName & arguments, const DataTypePtr &, size_t input_rows_count) const override
    {
        const DataTypePtr converted_argument_type = std::make_shared<DataTypeFloat64>();
        Columns converted_arguments;
        for (size_t i = 0; i < arguments.size(); ++i)
        {
            const auto & argument_type = arguments[i].type;
            ColumnPtr argument_column = arguments[i].column->convertToFullColumnIfConst();

            if (converted_argument_type->equals(*argument_type))
                converted_arguments.push_back(argument_column);
            else if (isNumber(argument_type))
                converted_arguments.push_back(convert_to_number_func.executeImpl(
                    {ColumnWithTypeAndName(argument_column, argument_type, "")}, converted_argument_type, input_rows_count));
            else if (isStringOrFixedString(argument_type))
                converted_arguments.push_back(parse_number_func.executeImpl(
                    {ColumnWithTypeAndName(argument_column, argument_type, "")}, converted_argument_type, input_rows_count));
            else
                throw Exception("Argument '" + std::string(argument_names[i]) + "' for function " + getName() + " must be number",
                    ErrorCodes::ILLEGAL_TYPE_OF_ARGUMENT);
        }

        auto res_column = Traits::ReturnColumnType::create(input_rows_count);
        auto & result_data = res_column->getData();

        const auto & year_data = typeid_cast<const ColumnFloat64 &>(*converted_arguments[0]).getData();
        const auto & month_data = typeid_cast<const ColumnFloat64 &>(*converted_arguments[1]).getData();
        const auto & day_data = typeid_cast<const ColumnFloat64 &>(*converted_arguments[2]).getData();

        const auto & date_lut = DateLUT::instance();

        for (size_t i = 0; i < input_rows_count; ++i) {
            const auto year = year_data[i];
            const auto month = month_data[i];
            const auto day = day_data[i];

            Int32 day_num = 0;

            if (year >= Traits::MIN_YEAR &&
                year <= Traits::MAX_YEAR &&
                month <= 12 &&
                day <= 31 &&
                YearMonthDayToSingleInt(year, month, day) <= Traits::MAX_DATE)
            {
                day_num = date_lut.makeDayNum(year, month, day);
            }

            result_data[i] = day_num;
        }

        return res_column;
    }

private:
    FunctionToFloat64 convert_to_number_func;
    FunctionToFloat64OrZero parse_number_func;
    FunctionOr or_func;
};

// makeDate(year, month, day)
struct MakeDateTraits
{
    static constexpr auto name = "makeDate";
    using ReturnDataType = DataTypeDate;
    using ReturnColumnType = ColumnUInt16;

    static constexpr auto MIN_YEAR = 1970;
    static constexpr auto MAX_YEAR = 2149;
    // This date has the maximum day number that fits in 16-bit uint
    static constexpr auto MAX_DATE = YearMonthDayToSingleInt(MAX_YEAR, 6, 6);
};

// makeDate32(year, month, day)
struct MakeDate32Traits
{
    static constexpr auto name = "makeDate32";
    using ReturnDataType = DataTypeDate32;
    using ReturnColumnType = ColumnInt32;

    static constexpr auto MIN_YEAR = 1925;
    static constexpr auto MAX_YEAR = 2283;
    static constexpr auto MAX_DATE = YearMonthDayToSingleInt(MAX_YEAR, 11, 11);
};

}

void registerFunctionsMakeDate(FunctionFactory & factory)
{
    factory.registerFunction<FunctionMakeDate<MakeDateTraits>>();
    factory.registerFunction<FunctionMakeDate<MakeDate32Traits>>();
}

}
