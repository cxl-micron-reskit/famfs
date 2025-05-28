Jenkins.instance.getAllItems(Job).each { job ->
    def scm = null
    def repoUrls = []

    // Freestyle and similar
    if (job instanceof hudson.model.AbstractProject) {
        scm = job.getScm()
    }
    // Pipeline (declarative or scripted)
    else if (job instanceof org.jenkinsci.plugins.workflow.job.WorkflowJob) {
        def defn = job.getDefinition()
        if (defn instanceof org.jenkinsci.plugins.workflow.cps.CpsScmFlowDefinition) {
            scm = defn.getScm()
        } else if (defn instanceof org.jenkinsci.plugins.workflow.cps.CpsFlowDefinition) {
            def script = defn.getScript()
            def matches = (script =~ /(?i)git\s+['"]([^'"]+)['"]/)
            matches.each {
                repoUrls << it[1]
            }
        }
    }

    // Extract URLs from GitSCM if present
    if (scm instanceof hudson.plugins.git.GitSCM) {
        scm.getUserRemoteConfigs().each { config ->
            repoUrls << config.getUrl()
        }
    }

    // Output
    if (!repoUrls.isEmpty()) {
        repoUrls.each { url ->
            println "${job.fullName} ${url}"
        }
    } else {
        println "${job.fullName} (no Git repo detected)"
    }
}

