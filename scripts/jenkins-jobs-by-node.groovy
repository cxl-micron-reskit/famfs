iimport jenkins.model.*
import hudson.model.*

def jenkins = Jenkins.instance

jenkins.computers.findAll { it.channel != null }.each { computer ->
    def node = computer.node
    def nodeName = computer.name ?: "master"
    println "=== Node: ${nodeName} ==="

    def builds = []

    jenkins.allItems.findAll { it instanceof Job }.each { job ->
        job.builds.each { build ->
            if (build.getBuiltOn() == node) {
                builds << build
            }
        }
    }

    def recentBuilds = builds.sort { -it.getTimeInMillis() }.take(8)

    recentBuilds.each { build ->
        println "  ${build.fullDisplayName} @ ${build.timestampString} [#${build.number}]"
    }

    if (recentBuilds.isEmpty()) {
        println "  No recent builds on this node."
    }

    println ""
}

