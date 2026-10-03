/* clang 插件 unused-vars 的演示素材（第 12 章）
 *
 * 预期告警:
 *   unused_var     (声明后从未引用)
 *   also_unused    (初始化了但从未引用)
 * 不应告警:
 *   used, param_x  (被引用; 参数默认不检查)
 */
int helper(int param_x) {
    int used = 1;
    int unused_var;          /* line 12: 应告警 */
    int also_unused = 5;     /* line 13: 应告警 */
    return used + param_x;
}

int main(void) {
    return helper(1) - 2;
}
