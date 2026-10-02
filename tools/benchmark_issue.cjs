const BENCHMARK_LABEL = 'benchmark';
const PERFORMANCE_LABEL = 'performance';
const REGRESSION_TITLE_PREFIX = '[Benchmark 回归]';
const API_PAGE_SIZE = 100;
const ISO_DATE_LENGTH = 10;
const PERCENT_SCALE = 100;
const PERCENT_PRECISION = 1;
const NOTIFIABLE_REPORT_STATUSES = new Set(['REGRESSION', 'ERROR']);

function regressionEvidence(report) {
  if (!NOTIFIABLE_REPORT_STATUSES.has(report.status)) {
    throw new Error('回归通知报告状态无效。');
  }
  if (!Array.isArray(report.regressions) || report.regressions.length === 0) {
    throw new Error('回归通知报告缺少有效回归证据。');
  }

  return report.regressions.map((regression) => {
    if (
      !regression ||
      typeof regression.group !== 'string' ||
      regression.group.length === 0 ||
      !Array.isArray(regression.items) ||
      regression.items.length === 0
    ) {
      throw new Error('回归通知报告包含无效的回归组证据。');
    }

    const groupPolicy = report.policy?.groups?.[regression.group];
    const tolerance = groupPolicy?.tolerance;
    if (typeof tolerance !== 'number' || !Number.isFinite(tolerance) || tolerance < 0) {
      throw new Error(`回归通知报告缺少 ${regression.group} 的有效容差。`);
    }

    return {
      group: regression.group,
      itemCount: regression.items.length,
      tolerance,
    };
  });
}

async function createOrReuseBenchmarkIssue({ github, context, core, report }) {
  if (report.report_ready !== true || report.notify_regression !== true) {
    return { created: false, reason: 'not-eligible' };
  }

  const regressions = regressionEvidence(report);
  const { owner, repo } = context.repo;
  const issues = await github.paginate(github.rest.issues.listForRepo, {
    owner,
    repo,
    state: 'open',
    labels: BENCHMARK_LABEL,
    per_page: API_PAGE_SIZE,
  });
  const duplicate = issues.find(
    (issue) => typeof issue.title === 'string' && issue.title.startsWith(REGRESSION_TITLE_PREFIX),
  );

  if (duplicate) {
    core.info(`已有未关闭 issue #${duplicate.number}，跳过创建。`);
    return { created: false, duplicateNumber: duplicate.number };
  }

  const date = new Date().toISOString().slice(0, ISO_DATE_LENGTH);
  const runUrl = `https://github.com/${owner}/${repo}/actions/runs/${context.runId}`;
  const summary = regressions
    .map(
      ({ group, itemCount, tolerance }) =>
        `- ${group}：${itemCount} 项有效回归，容差 ${(tolerance * PERCENT_SCALE).toFixed(PERCENT_PRECISION)}%。`,
    )
    .join('\n');
  await github.rest.issues.create({
    owner,
    repo,
    title: `${REGRESSION_TITLE_PREFIX} ${date}`,
    body: `性能门禁结论：${report.status}。\n\n${summary}\n\n完整用例、测量值和其他组错误见 [workflow run 的门禁报告](${runUrl})。`,
    labels: [PERFORMANCE_LABEL, BENCHMARK_LABEL],
  });
  return { created: true };
}

module.exports = { createOrReuseBenchmarkIssue };
