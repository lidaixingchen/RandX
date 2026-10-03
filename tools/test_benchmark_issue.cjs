const assert = require('node:assert/strict');
const test = require('node:test');
const { createOrReuseBenchmarkIssue } = require('./benchmark_issue.cjs');

const OTHER_ISSUE_COUNT = 20;
const API_PAGE_SIZE = 100;
const CREATED_ISSUE_NUMBER = 999;
const REPOSITORY_CONTEXT = {
  repo: { owner: 'owner', repo: 'project' },
  runId: 456,
};

function regressionReport() {
  return {
    report_ready: true,
    notify_regression: true,
    status: 'REGRESSION',
    policy: { groups: { general: { tolerance: 0.25 }, sampling_cpp17: { tolerance: 0.3 } } },
    regressions: [{ group: 'general', items: [{ name: 'case/a' }] }],
  };
}

function githubMock(issues) {
  const state = { listCalls: [], createCalls: [], infoCalls: [] };
  const listForRepo = async () => ({ data: [] });
  const github = {
    rest: {
      issues: {
        listForRepo,
        create: async (parameters) => {
          state.createCalls.push(parameters);
          return { data: { number: CREATED_ISSUE_NUMBER } };
        },
      },
    },
    paginate: async (endpoint, parameters) => {
      state.listCalls.push({ endpoint, parameters });
      return issues;
    },
  };
  const core = { info: (message) => state.infoCalls.push(message) };
  return { github, core, state, listForRepo };
}

test('从多个未关闭 issue 中找到已有回归记录', async () => {
  const duplicate = { number: OTHER_ISSUE_COUNT + 1, title: '[Benchmark 回归] 既有记录' };
  const issues = Array.from({ length: OTHER_ISSUE_COUNT }, (_, index) => ({
    number: index + 1,
    title: `其他 issue ${index + 1}`,
  }));
  issues.push(duplicate);
  const { github, core, state, listForRepo } = githubMock(issues);

  const result = await createOrReuseBenchmarkIssue({
    github,
    context: REPOSITORY_CONTEXT,
    core,
    report: regressionReport(),
  });

  assert.equal(result.duplicateNumber, duplicate.number);
  assert.equal(state.createCalls.length, 0);
  assert.equal(state.listCalls.length, 1);
  assert.equal(state.listCalls[0].endpoint, listForRepo);
  assert.deepEqual(state.listCalls[0].parameters, {
    owner: 'owner',
    repo: 'project',
    state: 'open',
    labels: 'benchmark',
    per_page: API_PAGE_SIZE,
  });
  assert.ok(state.infoCalls[0].includes(`已有未关闭 issue #${duplicate.number}`));
});

test('finds an existing benchmark regression beyond the REST API page boundary', async () => {
  const duplicate = { number: API_PAGE_SIZE + 1, title: '[Benchmark 回归] 后续分页记录' };
  const issues = Array.from({ length: API_PAGE_SIZE }, (_, index) => ({
    number: index + 1,
    title: `其他 issue ${index + 1}`,
  }));
  issues.push(duplicate);
  const { github, core, state } = githubMock(issues);

  const result = await createOrReuseBenchmarkIssue({
    github,
    context: REPOSITORY_CONTEXT,
    core,
    report: regressionReport(),
  });

  assert.equal(result.duplicateNumber, duplicate.number);
  assert.equal(state.createCalls.length, 0);
});

test('creates one issue with the configured labels when no open regression exists', async () => {
  const { github, core, state } = githubMock([]);

  const result = await createOrReuseBenchmarkIssue({
    github,
    context: REPOSITORY_CONTEXT,
    core,
    report: regressionReport(),
  });

  assert.deepEqual(result, { created: true });
  assert.equal(state.createCalls.length, 1);
  const [issue] = state.createCalls;
  assert.match(issue.title, /^\[Benchmark 回归\] \d{4}-\d{2}-\d{2}$/);
  assert.deepEqual(issue.labels, ['performance', 'benchmark']);
  assert.match(issue.body, /general：1 项有效回归，容差 25\.0%/);
  assert.match(issue.body, /actions\/runs\/456/);
});

test('does not query or create an issue after notification eligibility is lost', async () => {
  const { github, core, state } = githubMock([]);
  const report = { ...regressionReport(), notify_regression: false };

  const result = await createOrReuseBenchmarkIssue({
    github,
    context: REPOSITORY_CONTEXT,
    core,
    report,
  });

  assert.deepEqual(result, { created: false, reason: 'not-eligible' });
  assert.equal(state.listCalls.length, 0);
  assert.equal(state.createCalls.length, 0);
});

test('formats the issue only from report.regressions evidence', async () => {
  const { github, core, state } = githubMock([]);
  const report = {
    ...regressionReport(),
    groups: { unrelated: { regressions: [{ name: 'must-not-appear' }] } },
    regressions: [{ group: 'sampling_cpp17', items: [{ name: 'case/b' }, { name: 'case/c' }] }],
  };

  await createOrReuseBenchmarkIssue({ github, context: REPOSITORY_CONTEXT, core, report });

  const [issue] = state.createCalls;
  assert.match(issue.body, /sampling_cpp17：2 项有效回归，容差 30\.0%/);
  assert.doesNotMatch(issue.body, /unrelated|must-not-appear|general：/);
});

test('formats default_cpp17 regressions using the reported group tolerance', async () => {
  const { github, core, state } = githubMock([]);
  const report = {
    ...regressionReport(),
    policy: { groups: { default_cpp17: { tolerance: 0.25 } } },
    regressions: [{ group: 'default_cpp17', items: [{ name: 'BM_Default/case:1' }] }],
  };

  await createOrReuseBenchmarkIssue({ github, context: REPOSITORY_CONTEXT, core, report });

  const [issue] = state.createCalls;
  assert.match(issue.body, /default_cpp17：1 项有效回归，容差 25\.0%/);
});

test('keeps valid regression evidence when another group makes the report an error', async () => {
  const { github, core, state } = githubMock([]);
  const report = {
    ...regressionReport(),
    status: 'ERROR',
    groups: { sampling_cpp23: { status: 'ERROR', errors: [{ message: '缺少测量项' }] } },
  };

  await createOrReuseBenchmarkIssue({ github, context: REPOSITORY_CONTEXT, core, report });

  const [issue] = state.createCalls;
  assert.equal(state.createCalls.length, 1);
  assert.match(issue.body, /general：1 项有效回归，容差 25\.0%/);
  assert.doesNotMatch(issue.body, /sampling_cpp23|缺少测量项/);
});

test('rejects a notification report with no valid regression evidence', async () => {
  const { github, core, state } = githubMock([]);
  const report = { ...regressionReport(), regressions: [] };

  await assert.rejects(
    createOrReuseBenchmarkIssue({ github, context: REPOSITORY_CONTEXT, core, report }),
    /缺少有效回归证据/,
  );
  assert.equal(state.listCalls.length, 0);
  assert.equal(state.createCalls.length, 0);
});
