#!/bin/bash

BUILD_DIR=build
BIN_DIR=bin
HTML_DIR=html
REPORT_DIR=report
PROJECT_REALNAME=server-framework

cd ../
# rm -rf $BUILD_DIR
cmake -S . -B $BUILD_DIR -DCMAKE_BUILD_TYPE=Debug
cmake --build $BUILD_DIR --parallel $(nproc)

lcov -c -i -d ./ -o $REPORT_DIR/init.info # 初始化并创建基准数据文件

cd $BIN_DIR/
for file in $(ls | grep ut_)
do
    ./$file --gtest_output=xml:../$REPORT_DIR/ut-report_${file}.xml # 执行编译后的测试文件并生成测试用例数据报告
done
# mv asan.log* ../asan_${PROJECT_REALNAME}.log # 收集内存泄漏数据

cd ../$REPORT_DIR/

lcov -c -d ../ -o coverage_${PROJECT_REALNAME}.info # 收集单元测试数据

lcov -a init.info -a coverage_${PROJECT_REALNAME}.info -o coverage_total.info # 将单元测试数据汇总
lcov -r coverage_total.info "*/tests/*" "*/usr/include/*"  "*.h" "*build/*" "*/examples/*" -o final.info # 过滤不需要的信息

genhtml -o ../$HTML_DIR --title $PROJECT_REALNAME final.info # 将最终的单元测试覆盖率数据生成 html 文件