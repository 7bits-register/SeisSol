# How to contribute

## Workflow

### Step 1
You need a local fork of the the project. Please, go to our [main github page](https://github.com/SeisSol/SeisSol) and press the “fork” button in GitHub. This will create a copy of the SeisSol repository in your own GitHub account.

### Step 2
Clone the forked SeisSol project from github to your PC or laptop:
```
$ git clone --recurse-submodules https://github.com/<your github account>/SeisSol.git
```

Let's go into the new project’s directory:
```
$ cd SeisSol
```

At this point your local copy of the SeisSol project has only a single reference to your forked remote repository.

```
$ git remote -v
origin	https://github.com/<your_github_account>/SeisSol.git (fetch)
origin	https://github.com/<your_github_account>/SeisSol.git (push)
```

You need to set up a reference to the original remote SeisSol repository (called `upstream`) in order to be able to grab new changes from the SeisSol master branch. It will allow you to synchronize your contribution with us. 
```
$ git remote add upstream https://github.com/SeisSol/SeisSol.git
$ git remote -v
origin	https://github.com/<your_github_account>/SeisSol.git (fetch)
origin	https://github.com/<your_github_account>/SeisSol.git (push)
upstream	https://github.com/SeisSol/SeisSol.git (fetch)
upstream	https://github.com/SeisSol/SeisSol.git (push)
```

### Step 3
We highly recommend to clone the latest master branch of the SeisSol project, create a new branch out of it with a descriptive name and make your contribution to SeisSol there.
```
$ git checkout master
$ git pull upstream master
$ git branch <descriptive_branch_name>
$ git checkout <descriptive_branch_name>
```

feature/bugfix/extension/
